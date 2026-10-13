; Sums 5+4+3+2+1 into R1, then tests the stack and queue.
LOADI R0 5        ; counter
LOADI R1 0        ; sum
LOADI R2 1        ; constant 1
ADD R1 R0         ; address 3: sum += counter
SUB R0 R2         ; counter -= 1
JNZ R0 3          ; repeat while counter != 0
PUSH R1           ; stack test
POP R3            ; R3 = 15
ENQ R3            ; queue test
DEQ R4            ; R4 = 15
HALT
