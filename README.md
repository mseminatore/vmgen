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
- **Optional modules** — static or dynamic import system
- **Optional coroutines** — cooperative fiber-based concurrency
- **Generated tooling** — assembler, disassembler, and debug tracing
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

See `specs/minimal.yaml` for a minimal example, or `specs/full.yaml` for a
complete specification using all features.

## License

MIT
