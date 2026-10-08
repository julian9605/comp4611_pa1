# Forward both store base and store data
.reg R1 10
ADDI R2, R0, 7
ADDI R3, R1, 2
SW R2, 0(R3)
LW R4, 0(R3)
