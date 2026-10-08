# Not-taken EX branch still blocks IF; branch operands are initially ready.
.reg R1 1
.mem 0 1
LW R3, 0(R0)
BEQ R1, R0, done
ADDI R2, R0, 4
done: SW R2, 1(R0)
