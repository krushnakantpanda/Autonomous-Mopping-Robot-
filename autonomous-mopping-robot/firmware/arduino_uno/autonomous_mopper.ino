/*
  Autonomous Mopping Robot — reactive roaming prototype
  Board: Arduino Uno R3
  Libraries: Servo (bundled with Arduino IDE)

  DRY TEST FIRST. This is not a mapping/coverage robot or a safety-rated device.
  See docs/calibration-and-operation.md before enabling the pump.

  Drive: TB6612FNG; obstacle scan: HC-SR04 + servo; edge: analog IR modules;
  bumpers: switches to GND; pump: logic-level N-MOSFET + flyback diode.
*/
#include <Servo.h>

// TB6612FNG pins
const uint8_t PWMA = 5;  // PWM, left motor
const uint8_t AIN1 = 7;
const uint8_t AIN2 = 8;
const uint8_t PWMB = 6;  // PWM, right motor
const uint8_t BIN1 = 9;
const uint8_t BIN2 = 10;
const uint8_t STBY = 4;

// Sensors / outputs
const uint8_t SERVO_PIN = 3;
const uint8_t TRIG_PIN = 12;
const uint8_t ECHO_PIN = 11;
const uint8_t PUMP_PIN = 2;       // MOSFET gate, never drive pump directly
const uint8_t CLIFF_LEFT_PIN = A0;
const uint8_t CLIFF_RIGHT_PIN = A1;
const uint8_t BUMPER_LEFT_PIN = A2;   // switch to GND, internal pull-up
const uint8_t BUMPER_RIGHT_PIN = A3; // switch to GND, internal pull-up

// Tune to the actual chassis and sensor mounting.
const int DRIVE_SPEED = 125;        // PWM 0..255; keep low for first tests
const int TURN_SPEED = 125;
const int BACKUP_SPEED = 105;
const int OBSTACLE_CM = 25;
const int CLIFF_THRESHOLD = 250;    // calibrate! lower analog value = edge by default
const int SERVO_LEFT_DEG = 150;
const int SERVO_CENTER_DEG = 90;
const int SERVO_RIGHT_DEG = 30;
const unsigned long CLIFF_BACKUP_MS = 250;
const unsigned long CLIFF_TURN_MS = 420;
const unsigned long BUMPER_BACKUP_MS = 300;
const unsigned long BUMPER_TURN_MS = 500;
const unsigned long OBSTACLE_TURN_MS = 430;
const unsigned long WATER_PERIOD_MS = 4000; // one dose interval while moving
const unsigned long WATER_PULSE_MS = 250;   // calibrate output volume on a test surface
const unsigned long ECHO_TIMEOUT_US = 22000;

Servo scanServo;
unsigned long lastWaterPulse = 0;
unsigned long pumpOffAt = 0;
bool pumpRunning = false;

void setPump(bool on) {
  digitalWrite(PUMP_PIN, on ? HIGH : LOW);
  pumpRunning = on;
  if (!on) pumpOffAt = 0;
}

void stopDrive() {
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);
  digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW); digitalWrite(BIN2, LOW);
}

void driveForward(uint8_t speed) {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
  analogWrite(PWMA, speed);
  analogWrite(PWMB, speed);
}

void driveReverse(uint8_t speed) {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, LOW); digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, LOW); digitalWrite(BIN2, HIGH);
  analogWrite(PWMA, speed);
  analogWrite(PWMB, speed);
}

// Positive direction: left wheel reverse, right wheel forward (pivot right).
void pivotRight(uint8_t speed) {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, LOW); digitalWrite(AIN2, HIGH);
  digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
  analogWrite(PWMA, speed); analogWrite(PWMB, speed);
}

void pivotLeft(uint8_t speed) {
  digitalWrite(STBY, HIGH);
  digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW); digitalWrite(BIN2, HIGH);
  analogWrite(PWMA, speed); analogWrite(PWMB, speed);
}

bool cliffDetected() {
  // This default assumes a drop/void produces a LOWER analog reading.
  // If your sensor behaves oppositely, reverse the comparison after testing.
  const int left = analogRead(CLIFF_LEFT_PIN);
  const int right = analogRead(CLIFF_RIGHT_PIN);
  return (left < CLIFF_THRESHOLD) || (right < CLIFF_THRESHOLD);
}

bool bumperPressed() {
  return (digitalRead(BUMPER_LEFT_PIN) == LOW) ||
         (digitalRead(BUMPER_RIGHT_PIN) == LOW);
}

int readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  const unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) return 400; // no echo: treat as clear; bumper remains backup
  return (int)(duration / 58UL);
}

int scanDistance(int angle) {
  scanServo.write(angle);
  delay(240); // allow sensor bracket to settle
  // Median-ish average of three readings; ignore timeout as long range.
  long sum = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    sum += readDistanceCm();
    delay(35);
  }
  return (int)(sum / 3);
}

void doCliffRecovery() {
  stopDrive();
  setPump(false);
  delay(100);
  // Back away from the front edge, then turn. Check sensors only on the next loop.
  driveReverse(BACKUP_SPEED);
  delay(CLIFF_BACKUP_MS);
  stopDrive();
  pivotRight(TURN_SPEED);
  delay(CLIFF_TURN_MS);
  stopDrive();
  scanServo.write(SERVO_CENTER_DEG);
}

void doBumperRecovery() {
  stopDrive();
  setPump(false);
  driveReverse(BACKUP_SPEED);
  delay(BUMPER_BACKUP_MS);
  stopDrive();
  // Turn away from side when only one bumper is active.
  const bool leftHit = digitalRead(BUMPER_LEFT_PIN) == LOW;
  const bool rightHit = digitalRead(BUMPER_RIGHT_PIN) == LOW;
  if (leftHit && !rightHit) pivotRight(TURN_SPEED);
  else if (rightHit && !leftHit) pivotLeft(TURN_SPEED);
  else pivotRight(TURN_SPEED);
  delay(BUMPER_TURN_MS);
  stopDrive();
  scanServo.write(SERVO_CENTER_DEG);
}

void avoidObstacle() {
  stopDrive();
  setPump(false);
  const int leftCm = scanDistance(SERVO_LEFT_DEG);
  const int rightCm = scanDistance(SERVO_RIGHT_DEG);
  scanServo.write(SERVO_CENTER_DEG);
  delay(120);
  if (leftCm > rightCm) pivotLeft(TURN_SPEED);
  else pivotRight(TURN_SPEED); // ties choose right
  delay(OBSTACLE_TURN_MS);
  stopDrive();
}

void updateMoppingDose(bool movingForward) {
  const unsigned long now = millis();
  if (!movingForward) {
    setPump(false);
    return;
  }
  if (pumpRunning && now - pumpOffAt >= WATER_PULSE_MS) {
    setPump(false);
  }
  if (!pumpRunning && now - lastWaterPulse >= WATER_PERIOD_MS) {
    setPump(true);
    pumpOffAt = now;
    lastWaterPulse = now;
  }
}

void setup() {
  pinMode(PWMA, OUTPUT); pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(PWMB, OUTPUT); pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  pinMode(BUMPER_LEFT_PIN, INPUT_PULLUP);
  pinMode(BUMPER_RIGHT_PIN, INPUT_PULLUP);
  digitalWrite(STBY, LOW);
  setPump(false);
  stopDrive();
  scanServo.attach(SERVO_PIN);
  scanServo.write(SERVO_CENTER_DEG);
  Serial.begin(115200);
  delay(300);
  Serial.println(F("Mopper ready. DRY TEST; monitor cliff readings before autonomous run."));
  Serial.print(F("Cliff ADC L/R: "));
  Serial.print(analogRead(CLIFF_LEFT_PIN)); Serial.print('/');
  Serial.println(analogRead(CLIFF_RIGHT_PIN));
  lastWaterPulse = millis(); // do not dispense immediately after power-up
}

void loop() {
  // Safety inputs are checked before navigation and before any dose.
  if (cliffDetected()) {
    Serial.println(F("Cliff threshold detected: stop and recover"));
    doCliffRecovery();
    return;
  }
  if (bumperPressed()) {
    Serial.println(F("Bumper: stop and recover"));
    doBumperRecovery();
    return;
  }

  scanServo.write(SERVO_CENTER_DEG);
  const int frontCm = readDistanceCm();
  if (frontCm <= OBSTACLE_CM) {
    Serial.print(F("Obstacle at cm: ")); Serial.println(frontCm);
    avoidObstacle();
    return;
  }

  driveForward(DRIVE_SPEED);
  updateMoppingDose(true);
  delay(20);
}
