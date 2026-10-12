// Conceptual mopping robot footprint. Units mm. OpenSCAD model is a layout aid,
// not a fabrication-ready drawing: component footprints and fastener holes omitted.
$fn = 32;
chassis_w = 280;
chassis_d = 240;
plate_t = 4;
wheel_w = 22;
wheel_d = 64;
module box_at(x,y,z,sx,sy,sz) { translate([x,y,z]) cube([sx,sy,sz]); }
// Base plate
color("#94a3b8") box_at(0,0,0,chassis_w,chassis_d,plate_t);
// Drive wheels, left and right
color("#334155") {
  translate([-wheel_w,78,0]) cube([wheel_w,wheel_d,42]);
  translate([chassis_w,78,0]) cube([wheel_w,wheel_d,42]);
}
// Casters, front and rear (approximate envelopes)
color("#cbd5e1") {
  translate([chassis_w/2-16,18,0]) cylinder(h=20,r=16);
  translate([chassis_w/2-16,chassis_d-50,0]) cylinder(h=20,r=16);
}
// Tank and raised electronics deck represented as blocks
color("#38bdf8") box_at(65,86,plate_t,150,75,55);
color("#8b5cf6") box_at(72,118,65,136,62,20);
// Front sensor bracket, bumpers, down-facing cliff sensor footprints
color("#ef4444") box_at(42,4,plate_t,196,8,12);
color("#f59e0b") {
  box_at(40,28,plate_t,24,18,10);
  box_at(216,28,plate_t,24,18,10);
}
color("#2563eb") translate([chassis_w/2-25,10,plate_t]) cube([50,24,26]);
// Mop pad envelope on underside (displayed above base for clarity only)
color([0.2,0.8,0.4,0.55]) translate([42,145,plate_t+1]) cube([196,70,2]);
// Motor / wheel and tank fit should be checked against real hardware before fabrication.
