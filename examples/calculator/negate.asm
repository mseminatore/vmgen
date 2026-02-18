; negate.asm — Calculator VM example
; Demonstrates negation: -(5 - 3) = -2
;
; Constants:
;   [0] = 5
;   [1] = 3

PUSH_CONST 0    ; push 5
PUSH_CONST 1    ; push 3
SUB             ; 5 - 3 = 2
NEG             ; -2
HALT
