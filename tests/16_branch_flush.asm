# Taken EX branch: hold IF until resolved; skipped stores are never fetched.
.reg R1 1
.mem 0 17
BEQ R1, R1, target
SW R1, 0(R0)
SW R0, 0(R0)
target: ADDI R2, R0, 9
