# Lecture 5 slide 51: six RAW stalls
.freg F4 2
.freg F6 3
.freg F8 5
MUL.D F0, F4, F6
ADD.D F2, F0, F8
