# VMGen — VM Generator

[![CI](https://github.com/mseminatore/vmgen/actions/workflows/ci.yml/badge.svg)](https://github.com/mseminatore/vmgen/actions/workflows/ci.yml)

A tool that reads a declarative YAML or TOML specification describing a virtual machine's
instruction set, value types, and runtime features, then emits a complete,
standalone C implementation of that VM.

## Features

- **Declarative spec** — YAML or TOML input describing your VM's instruction set, types, and features
- **Action DSL** — describe opcode semantics with `pop`/`push`/`peek`/`jump` notation
- **Multiple dispatch strategies** — switch, function-pointer table, or computed gotos
- **Optional interned strings** — with FNV-1a hash table
- **Optional garbage collection** — mark-and-sweep or reference counting
- **Optional closures** — Lua-style by-reference upvalue capture
- **Optional modules** — static, dynamic, or combined import system with circular import detection
- **Optional coroutines** — cooperative fiber-based concurrency
- **Native function interface** — register C functions callable from bytecode
- **Error recovery** — configurable error handler callbacks with error codes
- **Debug stepping** — single-step execution, breakpoints, instruction callbacks
- **Bytecode verifier** — static validation of bytecode before execution
- **Bytecode optimizer** — constant folding, dead code elimination, peephole optimization
- **Source line mapping** — bytecode offset → source line lookup
- **Bytecode binary format** — save/load bytecode with typed constant pool
- **Generated tooling** — assembler, disassembler (stdout + buffer API), and debug tracing
- **Generated test harness** — smoke tests for the generated VM
- **Generated build system** — CMakeLists.txt for the output VM
- **Single-header option** — amalgamated `.h` file for easy embedding
- **Portable output** — generated C compiles on GCC, Clang, and MSVC with 0 warnings

## Building

This repo uses a git submodule for the `testy` test framework, so clone with
`--recurse-submodules` (or run `git submodule update --init --recursive` after a plain clone):

```bash
git clone --recurse-submodules https://github.com/mseminatore/vmgen.git
cmake -B build -S .
cmake --build build
```

## Running

```bash
# From YAML spec
./build/vmgen specs/minimal.yaml -o output/

# From TOML spec
./build/vmgen specs/full.toml -o output/
```

This generates a complete C project in `output/` with:

| File | Description |
|------|-------------|
| `opcodes.h` | Opcode enum, name table, operand counts, instruction sizes |
| `vm.h` / `vm.c` | VM struct, stack, constant pool, dispatch loop, error handler, debug stepping |
| `assembler.h` / `assembler.c` | Text mnemonic → bytecode assembler |
| `disasm.h` / `disasm.c` | Bytecode → text disassembler (stdout + buffer API) |
| `native.h` / `native.c` | Native C function registration and invocation |
| `strings.h` / `strings.c` | Interned string table (if enabled) |
| `gc.h` / `gc.c` | Garbage collector — mark-and-sweep or refcount (if enabled) |
| `closures.h` / `closures.c` | Closure/upvalue support (if enabled) |
| `modules.h` / `modules.c` | Module/import system with circular import detection (if enabled) |
| `fibers.h` / `fibers.c` | Coroutine/fiber support (if enabled) |
| `bytecode.h` / `bytecode.c` | Binary bytecode save/load with typed constants |
| `linemap.h` / `linemap.c` | Bytecode offset → source line mapping |
| `verify.h` / `verify.c` | Static bytecode validation (opcodes, bounds, jumps) |
| `optimizer.h` / `optimizer.c` | Bytecode optimizer (constant folding, dead code, peephole) |
| `test_vm.c` | Generated smoke tests for the VM |
| `CMakeLists.txt` | Build configuration for the generated VM |
| `<name>.h` | Single-header amalgamation (all code in one file) |

## Testing

```bash
cmake --build build
cd build && ctest --output-on-failure
```

This runs both:
- **Unit tests** (533 tests) — spec parsing, validation, code generation, action DSL
- **E2E integration tests** (4 tests) — generate → compile → run for each spec (minimal, calculator, full YAML, full TOML)

## Project Structure

```
vmgen/
├── CMakeLists.txt          # Build configuration
├── src/
│   ├── main.c              # CLI entry point
│   ├── spec/               # YAML/TOML parsing and spec model
│   ├── codegen/            # C code generation (16 generators)
│   └── util/               # Shared utilities (buffer)
├── specs/                  # Example VM specifications (.yaml, .toml)
├── examples/               # Sample assembly programs with walkthrough
├── tests/                  # Test suite (testy framework + CTest E2E)
└── third_party/            # Git submodule dependency (testy)
```

---

## Spec Format

The spec is the single input to VMGen. It can be **YAML** or **TOML** — the
format is auto-detected by file extension (`.yaml`/`.yml` or `.toml`).
It describes the VM name, stack layout, types, optional features, dispatch
strategy, and the full instruction set.

### Minimal Example

```yaml
vm:
  name: "minimal"
  version: "1.0"

  stack:
    type: "fixed"        # "fixed" or "dynamic"
    max_depth: 256
    value_type: "int64"  # "int64", "double", or "tagged_union"

  types:
    - int64

  features:
    interned_strings: false
    garbage_collection:
      enabled: false
    closures: false
    modules:
      enabled: false
    coroutines: false
    debug_tracing: false

  dispatch: "switch"     # "switch", "fnptr", "threaded", or "auto"

  instructions:
    - name: PUSH_CONST
      operands: [{ name: index, type: uint16 }]
      description: "Push constant at index onto stack"
      action: "push constants[$index]"

    - name: ADD
      operands: []
      description: "Pop two values, push their sum"
      action: "pop b; pop a; push a + b"
      stack_effect: -1

    - name: HALT
      operands: []
      description: "Stop execution"
      action: "halt"

  constants:
    - int64
```

### TOML Equivalent

The same spec in TOML:

```toml
[vm]
name = "minimal"
version = "1.0"
types = ["int64"]
dispatch = "switch"
constants = ["int64"]

[vm.stack]
type = "fixed"
max_depth = 256
value_type = "int64"

[vm.features]
interned_strings = false
closures = false
coroutines = false
debug_tracing = false

[vm.features.garbage_collection]
enabled = false

[vm.features.modules]
enabled = false

[[vm.instructions]]
name = "PUSH_CONST"
description = "Push constant at index onto stack"
action = "push constants[$index]"

[[vm.instructions.operands]]
name = "index"
type = "uint16"

[[vm.instructions]]
name = "HALT"
description = "Stop execution"
action = "halt"
```

### Stack Configuration

| Field | Values | Description |
|-------|--------|-------------|
| `type` | `fixed`, `dynamic` | Fixed uses a static array; dynamic grows as needed |
| `max_depth` | integer | Maximum stack depth |
| `value_type` | `int64`, `double`, `tagged_union` | Determines the stack slot type |

### Dispatch Modes

| Mode | Description |
|------|-------------|
| `switch` | Standard switch/case (portable, baseline) |
| `fnptr` | Function-pointer table (portable, MSVC-friendly) |
| `threaded` | Computed gotos (GCC/Clang only, fastest) |
| `auto` | Threaded if `__GNUC__`, otherwise fnptr |

### Value Types

| Type | Description |
|------|-------------|
| `tagged_union` | Tagged union with int64/double/bool/string/object variants |
| `double` | All values are doubles |
| `int64` | All values are 64-bit integers |

### Feature Toggles

| Feature | Description |
|---------|-------------|
| `interned_strings` | FNV-1a hash table for string deduplication |
| `garbage_collection.enabled` | Enable GC with object headers |
| `garbage_collection.strategy` | `"mark_sweep"` or `"refcount"` |
| `closures` | Lua-style upvalue capture (open on stack, closed on heap) |
| `modules.enabled` | Module/import system with circular import detection |
| `modules.loading` | `"static"`, `"dynamic"`, or `"combined"` |
| `coroutines` | Cooperative fiber-based concurrency (own stack per fiber) |
| `debug_tracing` | Emit debug trace output during instruction execution |
| `nan_boxing` | NaN-boxing for values (reserved, not yet implemented) |

### Operand Types

| Type | Size | Description |
|------|------|-------------|
| `uint8` | 1 byte | Unsigned 8-bit (arity, register index) |
| `uint16` | 2 bytes | Unsigned 16-bit (constant index, function index) |
| `int16` | 2 bytes | Signed 16-bit (jump offsets) |
| `uint32` | 4 bytes | Unsigned 32-bit (large constants) |

---

## Action DSL

Each instruction can define its behavior using a stack-effect DSL, an inline C
escape hatch, or both.

### DSL Syntax (`action:`)

Actions are semicolon-separated statements:

```yaml
action: "pop b; pop a; push a + b"
```

#### Statements

| Statement | Example | Description |
|-----------|---------|-------------|
| `pop <var>` | `pop a` | Pop stack top into variable `a` |
| `pop _` | `pop _` | Pop and discard |
| `push <expr>` | `push a + b` | Evaluate expression and push result |
| `peek <var>` | `peek a` | Read stack top without popping |
| `set <var> = <expr>` | `set x = a + 1` | Assign expression to variable |
| `jump <expr>` | `jump $offset` | Unconditional IP-relative jump |
| `jump_if_zero <var> <expr>` | `jump_if_zero a $offset` | Jump if variable is zero |
| `jump_if_nonzero <var> <expr>` | `jump_if_nonzero a $offset` | Jump if variable is nonzero |
| `call <expr> <expr>` | `call $func_index $arity` | Call function with N arguments |
| `return` | `return` | Return from current call frame |
| `halt` | `halt` | Stop VM execution |

#### Expressions

Expressions support arithmetic, comparison, unary, and constant-pool access:

| Syntax | Example | Description |
|--------|---------|-------------|
| `<var>` | `a` | DSL variable (from `pop`/`peek`/`set`) |
| `$<name>` | `$index` | Instruction operand reference |
| `<number>` | `42` | Integer literal |
| `a + b` | `a + b` | Arithmetic: `+`, `-`, `*`, `/`, `%` |
| `a == b` | `a == b` | Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=` |
| `-a` | `-a` | Unary negate |
| `!a` | `!a` | Unary logical NOT |
| `constants[<expr>]` | `constants[$index]` | Constant pool access |

#### Stack Effect Declaration

Optionally declare the net stack depth change for validation:

```yaml
action: "pop b; pop a; push a + b"
stack_effect: -1   # 2 pops, 1 push → net -1
```

VMGen computes the stack effect from the DSL and warns if the declared value
doesn't match.

### Inline C (`action_c:`)

For operations that can't be expressed in the DSL (I/O, type conversions,
system calls), use inline C:

```yaml
- name: PRINT
  description: "Pop and print top of stack"
  action_c: |
    double val = calc_vm_pop(vm);
    printf("%g\n", val);
```

In `action_c` blocks, use the generated API directly—`vm` is the VM pointer,
and all generated functions (push, pop, etc.) are available.

### GC Safety

When garbage collection is enabled, the generated dispatch loop automatically
wraps each opcode body with `gc_inhibit` / `gc_allow` calls. This prevents
the GC from running while popped values are held in C local variables (which
the GC's root scanner can't see). You don't need to handle this in your
action definitions.

---

## Generated APIs

All generated functions are prefixed with the VM `name` from the spec. The
examples below use `myvm` as the name.

### VM Core

```c
myvm_vm_t vm;
myvm_vm_init(&vm);

// Add constants to the pool
myvm_vm_add_constant(&vm, (myvm_value_t){ .as.i64 = 42 });

// Run bytecode
int result = myvm_vm_run(&vm, bytecode, bytecode_len);

// Stack operations (also used in action_c blocks)
myvm_vm_push(&vm, value);
myvm_value_t top = myvm_vm_pop(&vm);
```

### Assembler

The assembler converts textual mnemonics into bytecode. It understands all
instruction names and operand formats from the spec.

```c
myvm_assembler_t as;
myvm_asm_init(&as);

// Assemble a full program (newline-separated instructions)
myvm_asm_assemble(&as, "PUSH_CONST 0\nPUSH_CONST 1\nADD\nHALT\n");

// Or assemble one instruction at a time
myvm_asm_assemble_line(&as, "PUSH_CONST 0");
myvm_asm_assemble_line(&as, "HALT");

// Low-level byte emission
myvm_asm_emit_byte(&as, 0x01);
myvm_asm_emit_u16(&as, 0x0042);

// The assembled bytecode is in as.code (uint8_t*), as.code_len bytes
int result = myvm_vm_run(&vm, as.code, as.code_len);
```

### Disassembler

The disassembler pretty-prints bytecode as readable mnemonics.

```c
// Disassemble an entire bytecode buffer (prints to stdout)
myvm_disasm(bytecode, bytecode_len);
// Output: 0000  PUSH_CONST 0
//         0003  PUSH_CONST 1
//         0006  ADD
//         0007  HALT

// Disassemble a single instruction at an offset (returns new offset)
size_t next = myvm_disasm_instruction(bytecode, bytecode_len, offset);
```

### Native Functions

Register C functions that can be called from bytecode via the `CALL`
instruction.

```c
// Define a native function
myvm_value_t my_print(myvm_vm_t *vm, int arg_count) {
    myvm_value_t val = myvm_vm_pop(vm);
    printf("%lld\n", val.as.i64);
    return (myvm_value_t){0};
}

// Register it
myvm_native_table_t natives;
myvm_native_table_init(&natives);
myvm_native_register(&natives, "print", my_print, 1);

// Look up and call
int idx = myvm_native_find(&natives, "print");
myvm_native_call(&natives, idx, &vm, 1);

myvm_native_table_free(&natives);
```

### Garbage Collector

When `garbage_collection.enabled` is `true`, the GC manages heap objects.
Two strategies are available:

#### Mark-and-Sweep (`strategy: "mark_sweep"`)

```c
myvm_gc_t gc;
myvm_gc_init(&gc);

// Allocate a managed object
myvm_obj_t *obj = myvm_gc_alloc(&gc, MYVM_OBJ_STRING, sizeof(myvm_obj_t) + len);

// Mark reachable objects (called by your root-tracing code)
myvm_gc_mark_object(obj);

// Trigger a collection cycle (sweeps unmarked objects)
myvm_gc_collect(&gc);

// Temporarily prevent GC from running (e.g., during opcode execution)
myvm_gc_inhibit(&gc);   // increments inhibit_count
// ... work with raw pointers safely ...
myvm_gc_allow(&gc);     // decrements inhibit_count

myvm_gc_free(&gc);      // free all remaining objects
```

#### Reference Counting (`strategy: "refcount"`)

```c
myvm_gc_t gc;
myvm_gc_init(&gc);

myvm_obj_t *obj = myvm_gc_alloc(&gc, MYVM_OBJ_STRING, sizeof(myvm_obj_t) + len);
// obj starts with refcount = 1

myvm_gc_retain(obj);    // increment refcount
myvm_gc_release(obj);   // decrement; frees if refcount reaches 0

myvm_gc_free(&gc);
```

The VM struct includes a `myvm_gc_t *gc` pointer (NULL when GC is disabled).
The dispatch loop automatically inhibits GC during each opcode body (mark-sweep only)
to prevent collection while popped values are held in C locals.

### Interned Strings

When `interned_strings` is `true`, a hash table deduplicates strings so
pointer comparison replaces string comparison.

```c
myvm_string_table_t strings;
myvm_string_table_init(&strings);

// Intern a string (returns existing or creates new)
const char *s1 = myvm_string_intern(&strings, "hello", 5);
const char *s2 = myvm_string_intern(&strings, "hello", 5);
assert(s1 == s2);  // same pointer — interned

// Look up without creating
const char *found = myvm_string_find(&strings, "hello", 5);

myvm_string_table_free(&strings);
```

### Closures

When `closures` is `true`, Lua-style closures with by-reference upvalue
capture are generated. Open upvalues point to stack slots; they are "closed"
(copied to heap) when the enclosing scope exits.

```c
myvm_closure_t closure;
myvm_init_closure(&closure, func_index, upvalue_count);

// Capture a value
myvm_set_upvalue(&closure, 0, value);
int64_t val = myvm_get_upvalue(&closure, 0);

// Close an upvalue (move from stack to heap)
myvm_close_upvalue(&closure.upvalues[0]);

myvm_free_closure(&closure);
```

### Modules

When `modules.enabled` is `true`, the module system manages named bytecode
modules with their own global variables.

```c
myvm_module_table_t modules;
myvm_module_table_init(&modules);

// Load a module (bytecode + size)
myvm_module_load(&modules, "math", math_bytecode, math_len);

// Find a loaded module
myvm_module_t *mod = myvm_module_find(&modules, "math");

// Access module globals by index
myvm_module_set_global(mod, 0, value);
int64_t g = myvm_module_get_global(mod, 0);

myvm_module_table_free(&modules);
```

**Loading strategies** (set via `modules.loading` in the spec):

| Mode | Description |
|------|-------------|
| `static` | Modules are compiled in and registered at startup |
| `dynamic` | Modules loaded at runtime from bytecode files |
| `combined` | Both static and dynamic loading supported |

### Fibers / Coroutines

When `coroutines` is `true`, cooperative fibers are generated. Each fiber
has its own stack and can be suspended/resumed.

```c
myvm_fiber_t *fiber = myvm_fiber_new();

// Resume execution (passes a value to the fiber)
myvm_fiber_resume(fiber, input_value);

// Inside fiber code, yield back to caller
myvm_fiber_yield(fiber, result_value);

// Check fiber state
myvm_fiber_status_t status = myvm_fiber_status(fiber);
// MYVM_FIBER_CREATED, MYVM_FIBER_RUNNING, MYVM_FIBER_SUSPENDED, MYVM_FIBER_DEAD

myvm_fiber_free(fiber);
```

### Disassembler Buffer API

In addition to printing to stdout, the disassembler can write to a string buffer
for programmatic use (testing, logging, etc.):

```c
// Disassemble one instruction into a buffer
char buf[256];
size_t next = myvm_disasm_instruction_buf(bytecode, len, offset, buf, sizeof(buf));

// Disassemble entire bytecode into a malloc'd string
char *text = myvm_disasm_buf(bytecode, len);
printf("%s", text);
free(text);
```

### Error Recovery

The VM includes configurable error handling. By default, errors print to stderr.
You can install a custom handler:

```c
// Error codes: MYVM_ERR_STACK_OVERFLOW, MYVM_ERR_STACK_UNDERFLOW,
//              MYVM_ERR_UNKNOWN_OPCODE, MYVM_ERR_CONST_OVERFLOW,
//              MYVM_ERR_DIV_BY_ZERO, MYVM_ERR_TYPE_MISMATCH

void my_handler(void *userdata, myvm_error_t err, const char *msg) {
    fprintf(stderr, "VM error %d: %s\n", err, msg);
}

myvm_vm_set_error_handler(&vm, my_handler, NULL);

// After running, check the last error:
if (vm.last_error != MYVM_ERR_NONE) { /* handle */ }
```

### Debug Stepping

Single-step execution and breakpoints for debugger integration:

```c
// Prepare VM for stepping
vm.code = bytecode;
vm.code_len = len;
vm.running = 1;

// Execute one instruction at a time
while (vm.running) {
    int rc = myvm_vm_step(&vm);
    if (rc != 0) break;
    printf("ip=%zu, sp=%d\n", vm.ip, vm.sp);
}

// Set/clear breakpoints by bytecode offset
myvm_vm_set_breakpoint(&vm, 0x0010);
myvm_vm_clear_breakpoint(&vm, 0x0010);

// Instruction callback (called before each step)
void on_step(void *vm, size_t ip) {
    printf("about to execute at %zu\n", ip);
}
myvm_vm_set_step_callback(&vm, on_step);
```

### Bytecode Binary Format

Save and load bytecode with a typed constant pool:

```c
// Save: writes magic header (0x564D4743), version, constants, code
int rc = myvm_bc_save("program.bc", vm.constants.values, vm.constants.count,
                       bytecode, bytecode_len);

// Load: reads back constants and code
myvm_value_t constants[256];
int const_count;
uint8_t *code;
size_t code_len;
rc = myvm_bc_load("program.bc", constants, &const_count, &code, &code_len);

// ... use code ...
myvm_bc_free_code(code);
```

### Source Line Mapping

Map bytecode offsets to source line numbers for debugging:

```c
myvm_linemap_t map;
myvm_linemap_init(&map);

// Add entries during compilation (offset → line)
myvm_linemap_add(&map, 0, 1);    // offset 0 = line 1
myvm_linemap_add(&map, 3, 2);    // offset 3 = line 2
myvm_linemap_add(&map, 7, 5);    // offset 7 = line 5

// Query: binary search for largest offset ≤ target
int line = myvm_linemap_get_line(&map, 4);  // returns 2

myvm_linemap_free(&map);
```

### Bytecode Verifier

Statically validate bytecode before execution:

```c
myvm_verify_result_t result;
myvm_verify_init(&result);

int errors = myvm_verify(bytecode, len, const_count, &result);
if (errors > 0) {
    for (int i = 0; i < result.count; i++) {
        printf("offset %zu: %s\n", result.errors[i].offset,
               result.errors[i].message);
    }
}
myvm_verify_free(&result);
```

**Checks performed:**
- All opcodes are within valid range
- Each instruction fits within code bounds
- `PUSH_CONST` indices are within the constant pool
- Jump targets are within code bounds

### Bytecode Optimizer

Optimize bytecode in-place with three passes:

```c
myvm_opt_stats_t stats;
int total = myvm_optimize(code, code_len, &constants, &stats);

printf("Folded %d constants, eliminated %d dead bytes, %d peephole opts\n",
       stats.constants_folded, stats.dead_code_bytes, stats.peephole_opts);

/* Optional: compact by stripping NOP bytes */
size_t new_len = myvm_optimize_strip_nops(code, code_len);
```

**Optimization passes:**

| Pass | Description |
|------|-------------|
| Constant folding | `PUSH_CONST X, PUSH_CONST Y, ADD` → `PUSH_CONST Z` (adds result to pool) |
| Dead code elimination | NOP-fills unreachable bytes after `HALT` / `RETURN` |
| Peephole | Removes useless `PUSH_CONST` + `POP` sequences |

Replaced instructions are filled with `MYVM_OP_NOP` (`0xFE`) bytes, preserving all
offsets. The optional `strip_nops` pass compacts the bytecode and patches jump targets.

Only passes relevant to the spec are emitted — e.g., constant folding is omitted if
the spec has no arithmetic opcodes.

### Single-Header Amalgamation

VMGen generates a single-header file (`<name>.h`) containing all declarations
and implementations. Use it for easy embedding:

```c
// In exactly one .c file:
#define MYVM_IMPLEMENTATION
#include "myvm.h"

// In all other files:
#include "myvm.h"   // declarations only
```

---

## Editor Support

A JSON Schema is provided at `schemas/vmgen-spec.schema.json` for validation
and autocomplete in editors.

### VS Code (YAML)

Add to `.vscode/settings.json`:

```json
{
  "yaml.schemas": {
    "./schemas/vmgen-spec.schema.json": "specs/*.yaml"
  }
}
```

Requires the [YAML extension](https://marketplace.visualstudio.com/items?itemName=redhat.vscode-yaml).

### VS Code (inline)

Add a modeline at the top of your YAML spec:

```yaml
# yaml-language-server: $schema=../../schemas/vmgen-spec.schema.json
vm:
  name: "myvm"
  ...
```

### Other Editors

Any editor with YAML/JSON Schema support can use the schema file for
validation, autocomplete, and hover documentation of all spec fields.

---

## License

MIT
