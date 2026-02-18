; hello.asm — Minimal VM example
; Push two constants and add them: 10 + 32 = 42
;
; Before running, generate the VM and build it:
;   vmgen specs/minimal.yaml -o gen/
;   cd gen && mkdir build && cd build
;   cmake .. && cmake --build . --config Debug
;
; Then use the assembler API in your own program.

PUSH_CONST 0    ; push constant[0] (10)
PUSH_CONST 1    ; push constant[1] (32)
ADD             ; pop two, push sum (42)
HALT            ; stop
