# Fully pipelined DIV: independent divisions enter EX in consecutive cycles
.freg F2 12
.freg F4 3
.freg F8 20
.freg F10 4
DIV.D F0, F2, F4
DIV.D F6, F8, F10
