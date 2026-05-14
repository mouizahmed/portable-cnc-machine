%
(warning-demo.gcode)
(Designed to trigger parser warnings while still producing a preview.)

X10 Y10
G1 X0 Y0 F800
G2 X20 Y0 R10
G91
G1 X5 Y5
G90
G98 X25
G2 X30 Y10 I
G1 X40 Y10
@
M30
