; loop.asm — Full VM example
; Counts down from 5 to 0 using jumps
;
; Constants:
;   [0] = 5     (initial counter)
;   [1] = 1     (decrement value)
;   [2] = 0     (comparison target)
;
; Register usage:
;   top of stack = counter

PUSH_CONST 0        ; push 5 (counter)
; loop:                offset 3
PUSH_CONST 1        ; push 1
SUB                 ; counter - 1
PUSH_CONST 2        ; push 0
EQ                  ; counter == 0?
JUMP_IF_FALSE -10   ; jump back to offset 3 if not zero
HALT
