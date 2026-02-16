# VMGen — VM Generator

A tool that reads a declarative YAML specification describing a virtual machine's
instruction set, value types, and runtime features, then emits a complete,
standalone C implementation of that VM.

## Features

- **Declarative YAML spec** — define your VM's instruction set, types, and features
- **Action DSL** — describe opcode semantics with `pop`/`push`/`peek`/`jump` notation
- **Multiple dispatch strategies** — switch, function-pointer table, or computed gotos
- **Optional interned strings** — with FNV-1a hash table
- **Optional garbage collection** — mark-and-sweep with GC-safe opcode execution
- **Optional closures** — Lua-style by-reference upvalue capture
- **Optional modules** — static, dynamic, or combined import system
- **Optional coroutines** — cooperative fiber-based concurrency
- **Native function interface** — register C functions callable from bytecode
- **Generated tooling** — assembler, disassembler, and debug tracing
- **Generated build system** — CMakeLists.txt for the output VM
- **Portable output** — generated C compiles on GCC, Clang, and MSVC

## Building

```bash
cmake -B build -S .
cmake --build build
```

## Running

```bash
./build/vmgen specs/minimal.yaml -o output/
```

This generates a complete C project in `output/` with:

| File | Description |
|------|-------------|
| `opcodes.h` | Opcode enum, name table, operand counts, instruction sizes |
| `vm.h` / `vm.c` | VM struct, stack, constant pool, dispatch loop |
| `assembler.h` / `assembler.c` | Text mnemonic → bytecode assembler |
| `disasm.h` / `disasm.c` | Bytecode → text disassembler |
| `native.h` / `native.c` | Native C function registration and invocation |
| `strings.h` / `strings.c` | Interned string table (if enabled) |
| `gc.h` / `gc.c` | Garbage collector (if enabled) |
| `closures.h` / `closures.c` | Closure/upvalue support (if enabled) |
| `modules.h` / `modules.c` | Module/import system (if enabled) |
| `fibers.h` / `fibers.c` | Coroutine/fiber support (if enabled) |
| `CMakeLists.txt` | Build configuration for the generated VM |

## Testing

```bash
cmake --build build
cd build && ctest --output-on-failure
```

## Project Structure

```
vmgen/
├── CMakeLists.txt          # Build configuration
├── src/
│   ├── main.c              # CLI entry point
│   ├── spec/               # YAML parsing and spec model
│   ├── codegen/            # C code generation
│   └── util/               # Shared utilities
├── specs/                  # Example VM specifications
├── tests/                  # Test suite (testy framework)
└── third_party/            # Vendored dependencies
```

---

## Spec Format

The YAML spec is the single input to VMGen. It describes the VM name, stack
layout, types, optional features, dispatch strategy, and the full instruction
set.

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
| `garbage_collection.enabled` | Enable mark-and-sweep GC with object headers |
| `garbage_collection.strategy` | `"mark_sweep"` (ref counting planned) |
| `closures` | Lua-style upvalue capture (open on stack, closed on heap) |
| `modules.enabled` | Module/import system |
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

When `garbage_collection.enabled` is `true`, the GC manages heap objects with
a mark-and-sweep strategy.

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

The VM struct includes a `myvm_gc_t *gc` pointer (NULL when GC is disabled).
The dispatch loop automatically inhibits GC during each opcode body to prevent
collection while popped values are held in C locals.

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

---

## License

MIT
