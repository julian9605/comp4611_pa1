# Lecture 5 slide 49: one shared-port stall
.freg F2 2
.freg F4 3
.freg F8 4
.freg F10 5
MUL.D F0, F2, F4
ADD R1, R0, R0
SUB R2, R0, R0
ADD.D F6, F8, F10
