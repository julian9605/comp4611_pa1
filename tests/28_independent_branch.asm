# Independent taken branch: no fetching during ID2 and EX3.
.mem 0 17
BEQ R0, R0, target
SW R0, 0(R0)
SW R0, 0(R0)
target: ADDI R1, R0, 9
