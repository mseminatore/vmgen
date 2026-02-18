; constants.asm — Full VM example
; Demonstrates constant pool with mixed arithmetic
;
; Computes: (100 + 200) - (50 * 2) = 200
;
; Constants:
;   [0] = 100
;   [1] = 200
;   [2] = 50
;   [3] = 2

PUSH_CONST 0    ; push 100
PUSH_CONST 1    ; push 200
ADD             ; 100 + 200 = 300
PUSH_CONST 2    ; push 50
PUSH_CONST 3    ; push 2
MUL             ; 50 * 2 = 100
SUB             ; 300 - 100 = 200
HALT
