# VMGen — VM Generator

A tool that reads a declarative YAML specification describing a virtual machine's
instruction set, value types, and runtime features, then emits a complete,
standalone C implementation of that VM.

## Features

- **Declarative YAML spec** — define your VM's instruction set, types, and features
- **Multiple dispatch strategies** — switch, function-pointer table, or computed gotos
- **Optional interned strings** — with FNV-1a hash table
- **Optional garbage collection** — mark-and-sweep or reference counting
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

## Spec Format

See `specs/minimal.yaml` for a minimal example, `specs/calculator.yaml` for a
numeric-only VM, or `specs/full.yaml` for a complete specification using all
features.

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

## License

MIT
