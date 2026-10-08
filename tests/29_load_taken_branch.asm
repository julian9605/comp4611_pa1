# Older load and an independent taken branch; no branch-operand forwarding.
.reg R3 1
.mem 0 1
.mem 1 17
LW R1, 0(R0)
BEQ R3, R3, target
SW R1, 1(R0)
SW R0, 1(R0)
target: ADDI R2, R0, 9
