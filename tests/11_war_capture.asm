# Operands are captured at issue; no WAR stall
.freg F2 12
.freg F4 3
.freg F6 4
.freg F8 5
DIV.D F0, F2, F4
ADD.D F2, F6, F8
