; arithmetic.asm — Calculator VM example
; Evaluates: (5 + 3) * 2 - 1 = 15
;
; Constants:
;   [0] = 5
;   [1] = 3
;   [2] = 2
;   [3] = 1

PUSH_CONST 0    ; push 5
PUSH_CONST 1    ; push 3
ADD             ; 5 + 3 = 8
PUSH_CONST 2    ; push 2
MUL             ; 8 * 2 = 16
PUSH_CONST 3    ; push 1
SUB             ; 16 - 1 = 15
HALT
