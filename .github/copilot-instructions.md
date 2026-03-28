# VMGen — Copilot Instructions

## Build & Test

```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Run all tests (unit + e2e)
cd build && ctest --output-on-failure

# Run only unit tests (533 tests via testy framework)
./build/vmgen_tests          # Linux/macOS
.\build\Debug\vmgen_tests.exe  # Windows

# Run a single e2e test (generate → compile → run)
ctest --test-dir build -R e2e_minimal --output-on-failure

# Manual generation
./build/vmgen specs/full.yaml -o output/
```

The testy framework does not support filtering individual unit tests by name — the full suite always runs.

E2E tests (`e2e_minimal`, `e2e_calculator`, `e2e_full`, `e2e_full_toml`) each generate a VM from a spec, compile the output, and run the generated smoke tests.

## Architecture

VMGen reads a declarative YAML or TOML spec describing a VM's instruction set, then emits a complete standalone C implementation.

### Pipeline

```
spec file (YAML/TOML)
  → parser (src/spec/parser.c or parser_toml.c)
    → vmgen_spec_t (src/spec/model.h)
      → validate (src/spec/validate.c)
        → 16 code generators (src/codegen/gen_*.c)
          → complete C project in output directory
```

`main.c` orchestrates this: parse → validate → generate each component in sequence. The single-header generator (`gen_single_header.c`) must run last because it amalgamates all previously generated files.

### Key Data Structures

- **`vmgen_spec_t`** — the central model holding the entire parsed spec: VM name, stack config, type flags, features, up to 256 instructions with operands and action DSL strings. ~280KB per instance due to fixed-size arrays; tests set 16MB stack size to accommodate this.
- **`vmgen_buf_t`** — self-growing string buffer used by all code generators for emission. Init → append/appendf → write_file → free.
- **`vmgen_instr_t`** — single instruction definition with operands, action DSL string, optional inline C, typed action dispatch, and stack effect.

### Code Generation Pattern

Every `gen_*.c` file follows the same pattern:

```c
int vmgen_gen_<feature>(const vmgen_spec_t *spec, const char *output_dir) {
    vmgen_buf_t buf;
    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "filename.h", spec);   // from emit.c
    vmgen_emit_guard_open(&buf, "GUARD_NAME");
    // ... emit content via vmgen_buf_append / vmgen_buf_appendf ...
    vmgen_emit_guard_close(&buf, "GUARD_NAME");
    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    return rc;
}
```

When adding a new generator, follow this pattern and add the call to the sequence in `main.c` (before `gen_single_header`).

### Action DSL

Instructions define behavior via a stack-effect mini-language parsed by `action_parser.c` and emitted as C by `action_emit.c`:

```yaml
action: "pop b; pop a; push a + b"    # ADD instruction
action: "push constants[$index]"       # PUSH_CONST with operand reference
action: "pop _; jump $target"          # discard + branch
```

Statements: `pop`, `push`, `peek`, `set`, `jump`, `jump_if_zero`, `jump_if_nonzero`, `call`, `return`, `halt`, `try_begin`, `try_end`, `catch`, `throw`. Operands are referenced as `$name`. Constants via `constants[...]`.

Instructions can also use `action_c` for inline C, or `typed_actions` for type-dispatched behavior on tagged unions.

## Conventions

### Naming

- **Types**: `vmgen_<name>_t` (e.g., `vmgen_buf_t`, `vmgen_spec_t`)
- **Public functions**: `vmgen_<module>_<action>()` (e.g., `vmgen_buf_init()`, `vmgen_parse_spec()`)
- **Constants/macros**: `VMGEN_<NAME>` (e.g., `VMGEN_MAX_NAME`, `VMGEN_TYPE_INT64`)
- **Static helpers**: `static` with lowercase names
- **Generated code** uses the VM's name as prefix (e.g., spec `name: "myvm"` → `myvm_vm_init()`, `MYVM_OP_ADD`)

### Error Handling

All functions return `int` — `0` for success, non-zero for error. Errors are logged to stderr via `fprintf(stderr, ...)`. Check return codes immediately and propagate failures.

### Memory Management

Manual `malloc`/`free`. Heap-allocated strings (action DSL) use `vmgen_strdup()` for MSVC portability. Always call `vmgen_spec_free()` to clean up a parsed spec. `vmgen_buf_t` grows exponentially (cap × 2) and must be freed with `vmgen_buf_free()`.

### Platform Portability

C11 standard. Use `#ifdef _WIN32` for Windows-specific code and `#ifdef _MSC_VER` for MSVC specifics (e.g., `_strdup`). Compiler warnings are strict: `-Wall -Wextra -Wpedantic` on GCC/Clang, `/W4` on MSVC. Generated output must compile with zero warnings on all three compilers.

### Spec Format

Specs can be YAML or TOML (auto-detected by file extension). Required fields: `vm.name`, `vm.version`, `vm.stack`, `vm.instructions`. Features (strings, GC, closures, modules, coroutines) are all optional with sensible defaults. See `schemas/vmgen-spec.schema.json` for the full schema and `specs/` for examples.
