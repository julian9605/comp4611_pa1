# Lecture 5 slide 50: three WAW stalls
.freg F2 2
.freg F4 3
.freg F6 4
.freg F8 5
MUL.D F0, F2, F4
ADD.D F0, F6, F8
