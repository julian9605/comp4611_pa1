# Tutorial 2: compare all three forwarding paths enabled against all disabled.
.reg R3 3
.reg R4 0
.reg R6 1
.mem 0 5
.mem 4 17
LW R1, 0(R4)
SW R1, 4(R4)
ADD R2, R1, R3
SUB R5, R2, R6
ADDI R4, R4, 8
