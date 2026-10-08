# Branch WB would collide with MUL at 11; resolve must move with final ID.
.freg F2 2
.freg F4 3
.mem 0 17
MUL.D F0, F2, F4
ADD R0, R0, R0
ADD R0, R0, R0
ADD R0, R0, R0
ADD R0, R0, R0
ADD R0, R0, R0
BEQ R0, R0, target
SW R0, 0(R0)
SW R0, 0(R0)
target: ADDI R1, R0, 9
