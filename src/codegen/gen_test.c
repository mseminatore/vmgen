#include "gen_test.h"
#include "emit.h"
#include "../util/buffer.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _MSC_VER
#define vmgen_stricmp _stricmp
#else
#define vmgen_stricmp strcasecmp
#endif

/* Helper: uppercase name into buf */
static void to_upper(const char *src, char *dst, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Find an instruction by name (case-insensitive), return its opcode index or -1 */
static int find_instr(const vmgen_spec_t *spec, const char *name) {
    for (int i = 0; i < spec->instruction_count; i++) {
        if (vmgen_stricmp(spec->instructions[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Check if spec has a PUSH_CONST + HALT at minimum for meaningful tests */
static bool has_basic_ops(const vmgen_spec_t *spec) {
    return find_instr(spec, "PUSH_CONST") >= 0 && find_instr(spec, "HALT") >= 0;
}

int vmgen_gen_test(const vmgen_spec_t *spec, const char *output_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_vm.c", output_dir);

    char NAME[VMGEN_MAX_NAME];
    to_upper(spec->name, NAME, sizeof(NAME));

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    /* File header */
    vmgen_buf_appendf(&buf,
        "/*\n"
        " * test_vm.c\n"
        " * Generated smoke tests for VM '%s' v%s\n"
        " * DO NOT EDIT — regenerate from spec instead.\n"
        " */\n\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include \"vm.h\"\n"
        "#include \"opcodes.h\"\n"
        "#include \"assembler.h\"\n"
        "#include \"disasm.h\"\n"
        "#include \"bytecode.h\"\n",
        spec->name, spec->version);

    if (spec->features.interned_strings) {
        vmgen_buf_append(&buf, "#include \"strings.h\"\n");
    }
    if (spec->features.gc_enabled) {
        vmgen_buf_append(&buf, "#include \"gc.h\"\n");
    }

    /* Simple test framework macros */
    vmgen_buf_appendf(&buf,
        "\nstatic int tests_run = 0;\n"
        "static int tests_failed = 0;\n\n"
        "#define TEST_ASSERT(cond, msg) do { \\\n"
        "    tests_run++; \\\n"
        "    if (!(cond)) { \\\n"
        "        fprintf(stderr, \"FAIL: %%s (%%s:%%d)\\n\", msg, __FILE__, __LINE__); \\\n"
        "        tests_failed++; \\\n"
        "    } \\\n"
        "} while(0)\n\n"
        "#define TEST_ASSERT_INT(actual, expected, msg) do { \\\n"
        "    tests_run++; \\\n"
        "    if ((actual) != (expected)) { \\\n"
        "        fprintf(stderr, \"FAIL: %%s — expected %%lld, got %%lld (%%s:%%d)\\n\", \\\n"
        "            msg, (long long)(expected), (long long)(actual), __FILE__, __LINE__); \\\n"
        "        tests_failed++; \\\n"
        "    } \\\n"
        "} while(0)\n\n");

    /* === Test: VM init/basic state === */
    vmgen_buf_appendf(&buf,
        "static void test_vm_init(void) {\n"
        "    %s_vm_t vm;\n"
        "    %s_vm_init(&vm);\n"
        "    TEST_ASSERT(vm.sp == 0, \"vm init: sp should be 0\");\n"
        "    TEST_ASSERT(vm.ip == 0, \"vm init: ip should be 0\");\n"
        "    %s_vm_free(&vm);\n"
        "}\n\n",
        spec->name, spec->name, spec->name);

    /* === Test: push/pop === */
    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        vmgen_buf_appendf(&buf,
            "static void test_push_pop(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    %s_vm_push(&vm, %s_INT64(42));\n"
            "    TEST_ASSERT(vm.sp == 1, \"push: sp should be 1\");\n"
            "    %s_value_t val = %s_vm_pop(&vm);\n"
            "    TEST_ASSERT(vm.sp == 0, \"pop: sp should be 0\");\n"
            "    TEST_ASSERT_INT(val.as.i64, 42, \"pop: value should be 42\");\n"
            "    %s_vm_free(&vm);\n"
            "}\n\n",
            spec->name, spec->name, spec->name, NAME,
            spec->name, spec->name, spec->name);
    } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
        vmgen_buf_appendf(&buf,
            "static void test_push_pop(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    %s_vm_push(&vm, 42.0);\n"
            "    TEST_ASSERT(vm.sp == 1, \"push: sp should be 1\");\n"
            "    double val = %s_vm_pop(&vm);\n"
            "    TEST_ASSERT(vm.sp == 0, \"pop: sp should be 0\");\n"
            "    TEST_ASSERT(val == 42.0, \"pop: value should be 42.0\");\n"
            "    %s_vm_free(&vm);\n"
            "}\n\n",
            spec->name, spec->name, spec->name, spec->name, spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "static void test_push_pop(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    %s_vm_push(&vm, 42);\n"
            "    TEST_ASSERT(vm.sp == 1, \"push: sp should be 1\");\n"
            "    int64_t val = %s_vm_pop(&vm);\n"
            "    TEST_ASSERT(vm.sp == 0, \"pop: sp should be 0\");\n"
            "    TEST_ASSERT_INT(val, 42, \"pop: value should be 42\");\n"
            "    %s_vm_free(&vm);\n"
            "}\n\n",
            spec->name, spec->name, spec->name, spec->name, spec->name);
    }

    /* === Test: constants === */
    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        vmgen_buf_appendf(&buf,
            "static void test_constants(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    int idx = %s_vm_add_constant(&vm, %s_INT64(100));\n"
            "    TEST_ASSERT(idx == 0, \"first constant index should be 0\");\n"
            "    int idx2 = %s_vm_add_constant(&vm, %s_INT64(200));\n"
            "    TEST_ASSERT(idx2 == 1, \"second constant index should be 1\");\n"
            "    TEST_ASSERT_INT(vm.constants.values[0].as.i64, 100, \"constant 0 should be 100\");\n"
            "    TEST_ASSERT_INT(vm.constants.values[1].as.i64, 200, \"constant 1 should be 200\");\n"
            "}\n\n",
            spec->name, spec->name, spec->name, NAME,
            spec->name, NAME);
    } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
        vmgen_buf_appendf(&buf,
            "static void test_constants(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    int idx = %s_vm_add_constant(&vm, 100.0);\n"
            "    TEST_ASSERT(idx == 0, \"first constant index should be 0\");\n"
            "    int idx2 = %s_vm_add_constant(&vm, 200.0);\n"
            "    TEST_ASSERT(idx2 == 1, \"second constant index should be 1\");\n"
            "}\n\n",
            spec->name, spec->name, spec->name, spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "static void test_constants(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    int idx = %s_vm_add_constant(&vm, 100);\n"
            "    TEST_ASSERT(idx == 0, \"first constant index should be 0\");\n"
            "    int idx2 = %s_vm_add_constant(&vm, 200);\n"
            "    TEST_ASSERT(idx2 == 1, \"second constant index should be 1\");\n"
            "}\n\n",
            spec->name, spec->name, spec->name, spec->name);
    }

    /* === Test: HALT instruction === */
    if (has_basic_ops(spec)) {
        vmgen_buf_appendf(&buf,
            "static void test_halt(void) {\n"
            "    %s_vm_t vm;\n"
            "    %s_vm_init(&vm);\n"
            "    uint8_t code[] = { %s_OP_HALT };\n"
            "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
            "    TEST_ASSERT(rc == 0, \"HALT should return 0\");\n"
            "}\n\n",
            spec->name, spec->name, NAME, spec->name);
    }

    /* === Test: PUSH_CONST + HALT === */
    if (has_basic_ops(spec)) {
        if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
            vmgen_buf_appendf(&buf,
                "static void test_push_const(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, %s_INT64(99));\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,  /* push constant 0 */\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"push_const+halt should return 0\");\n"
                "    TEST_ASSERT(vm.sp == 1, \"sp should be 1 after push_const\");\n"
                "    TEST_ASSERT_INT(vm.stack[0].as.i64, 99, \"stack[0] should be 99\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, NAME,
                NAME, NAME, spec->name);
        } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
            vmgen_buf_appendf(&buf,
                "static void test_push_const(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, 99.0);\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"push_const+halt should return 0\");\n"
                "    TEST_ASSERT(vm.sp == 1, \"sp should be 1 after push_const\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, NAME, NAME, spec->name);
        } else {
            vmgen_buf_appendf(&buf,
                "static void test_push_const(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, 99);\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"push_const+halt should return 0\");\n"
                "    TEST_ASSERT(vm.sp == 1, \"sp should be 1 after push_const\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, NAME, NAME, spec->name);
        }
    }

    /* === Test: ADD (if present) === */
    if (find_instr(spec, "ADD") >= 0 && has_basic_ops(spec)) {
        if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
            vmgen_buf_appendf(&buf,
                "static void test_add(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, %s_INT64(10));\n"
                "    %s_vm_add_constant(&vm, %s_INT64(20));\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_PUSH_CONST, 1, 0,\n"
                "        %s_OP_ADD,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"add should return 0\");\n"
                "    TEST_ASSERT(vm.sp == 1, \"add: sp should be 1\");\n"
                "    TEST_ASSERT_INT(vm.stack[0].as.i64, 30, \"10 + 20 should be 30\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, NAME,
                spec->name, NAME, NAME, NAME, NAME, NAME, spec->name);
        } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
            vmgen_buf_appendf(&buf,
                "static void test_add(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, 10.0);\n"
                "    %s_vm_add_constant(&vm, 20.0);\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_PUSH_CONST, 1, 0,\n"
                "        %s_OP_ADD,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"add should return 0\");\n"
                "    TEST_ASSERT(vm.sp == 1, \"add: sp should be 1\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, spec->name,
                NAME, NAME, NAME, NAME, spec->name);
        } else {
            vmgen_buf_appendf(&buf,
                "static void test_add(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, 10);\n"
                "    %s_vm_add_constant(&vm, 20);\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_PUSH_CONST, 1, 0,\n"
                "        %s_OP_ADD,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"add should return 0\");\n"
                "    TEST_ASSERT(vm.sp == 1, \"add: sp should be 1\");\n"
                "    TEST_ASSERT_INT(vm.stack[0], 30, \"10 + 20 should be 30\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, spec->name,
                NAME, NAME, NAME, NAME, spec->name);
        }
    }

    /* === Test: SUB (if present) === */
    if (find_instr(spec, "SUB") >= 0 && has_basic_ops(spec)) {
        if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
            vmgen_buf_appendf(&buf,
                "static void test_sub(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, %s_INT64(50));\n"
                "    %s_vm_add_constant(&vm, %s_INT64(30));\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_PUSH_CONST, 1, 0,\n"
                "        %s_OP_SUB,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"sub should return 0\");\n"
                "    TEST_ASSERT_INT(vm.stack[0].as.i64, 20, \"50 - 30 should be 20\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, NAME,
                spec->name, NAME, NAME, NAME, NAME, NAME, spec->name);
        } else if (spec->stack.value_type == VMGEN_VALUE_INT64) {
            vmgen_buf_appendf(&buf,
                "static void test_sub(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, 50);\n"
                "    %s_vm_add_constant(&vm, 30);\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_PUSH_CONST, 1, 0,\n"
                "        %s_OP_SUB,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_vm_run(&vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"sub should return 0\");\n"
                "    TEST_ASSERT_INT(vm.stack[0], 20, \"50 - 30 should be 20\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, spec->name,
                NAME, NAME, NAME, NAME, spec->name);
        }
    }

    /* === Test: assembler round-trip (if has basic ops) === */
    if (has_basic_ops(spec)) {
        vmgen_buf_appendf(&buf,
            "static void test_assembler(void) {\n"
            "    %s_assembler_t as;\n"
            "    %s_asm_init(&as);\n"
            "    int rc = %s_asm_assemble(&as, \"PUSH_CONST 0\\nHALT\\n\");\n"
            "    TEST_ASSERT(rc == 0, \"assembler should succeed\");\n"
            "    TEST_ASSERT(as.len > 0, \"assembler should produce bytecode\");\n"
            "    TEST_ASSERT(as.code[0] == %s_OP_PUSH_CONST, \"first opcode should be PUSH_CONST\");\n"
            "}\n\n",
            spec->name, spec->name, spec->name, NAME);
    }

    /* === Test: disassembler (if has basic ops) === */
    if (has_basic_ops(spec)) {
        vmgen_buf_appendf(&buf,
            "static void test_disassembler(void) {\n"
            "    uint8_t code[] = { %s_OP_HALT };\n"
            "    char out[256];\n"
            "    int len = %s_disasm_buf(code, sizeof(code), out, sizeof(out));\n"
            "    TEST_ASSERT(len > 0, \"disasm_buf should produce output\");\n"
            "    TEST_ASSERT(strstr(out, \"HALT\") != NULL, \"disasm should contain HALT\");\n"
            "}\n\n",
            NAME, spec->name);
    }

    /* === Test: string interning (if enabled) === */
    if (spec->features.interned_strings) {
        if (spec->features.gc_enabled) {
            vmgen_buf_appendf(&buf,
                "static void test_string_intern(void) {\n"
                "    %s_gc_t gc;\n"
                "    %s_gc_init(&gc);\n"
                "    %s_string_table_t table;\n"
                "    %s_string_table_init(&table);\n"
                "    table.gc = &gc;\n"
                "    const %s_string_t *s1 = %s_string_intern(&table, \"hello\", 5);\n"
                "    const %s_string_t *s2 = %s_string_intern(&table, \"hello\", 5);\n"
                "    TEST_ASSERT(s1 == s2, \"interning same string should return same pointer\");\n"
                "    const %s_string_t *s3 = %s_string_intern(&table, \"world\", 5);\n"
                "    TEST_ASSERT(s1 != s3, \"different strings should have different pointers\");\n"
                "    %s_string_table_free(&table);\n"
                "    %s_gc_free(&gc);\n"
                "}\n\n",
                spec->name, spec->name, spec->name, spec->name,
                spec->name, spec->name, spec->name, spec->name,
                spec->name, spec->name, spec->name, spec->name);
        } else {
            vmgen_buf_appendf(&buf,
                "static void test_string_intern(void) {\n"
                "    %s_string_table_t table;\n"
                "    %s_string_table_init(&table);\n"
                "    const %s_string_t *s1 = %s_string_intern(&table, \"hello\", 5);\n"
                "    const %s_string_t *s2 = %s_string_intern(&table, \"hello\", 5);\n"
                "    TEST_ASSERT(s1 == s2, \"interning same string should return same pointer\");\n"
                "    const %s_string_t *s3 = %s_string_intern(&table, \"world\", 5);\n"
                "    TEST_ASSERT(s1 != s3, \"different strings should have different pointers\");\n"
                "    %s_string_table_free(&table);\n"
                "}\n\n",
                spec->name, spec->name, spec->name, spec->name,
                spec->name, spec->name, spec->name, spec->name,
                spec->name);
        }
    }

    /* === Test: bytecode save/load round-trip === */
    if (has_basic_ops(spec)) {
        if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
            vmgen_buf_appendf(&buf,
                "static void test_bytecode_roundtrip(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, %s_INT64(42));\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_bc_save(\"_test_roundtrip.bc\", &vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"bc_save should succeed\");\n"
                "    %s_vm_t vm2;\n"
                "    %s_vm_init(&vm2);\n"
                "    uint8_t *loaded = NULL;\n"
                "    size_t loaded_len = 0;\n"
                "    rc = %s_bc_load(\"_test_roundtrip.bc\", &vm2, &loaded, &loaded_len);\n"
                "    TEST_ASSERT(rc == 0, \"bc_load should succeed\");\n"
                "    TEST_ASSERT(loaded_len == sizeof(code), \"loaded code length should match\");\n"
                "    TEST_ASSERT(loaded[0] == %s_OP_PUSH_CONST, \"loaded first opcode should match\");\n"
                "    %s_bc_free_code(loaded);\n"
                "    remove(\"_test_roundtrip.bc\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name, NAME,
                NAME, NAME, spec->name, spec->name, spec->name,
                spec->name, NAME, spec->name);
        } else {
            vmgen_buf_appendf(&buf,
                "static void test_bytecode_roundtrip(void) {\n"
                "    %s_vm_t vm;\n"
                "    %s_vm_init(&vm);\n"
                "    %s_vm_add_constant(&vm, 42);\n"
                "    uint8_t code[] = {\n"
                "        %s_OP_PUSH_CONST, 0, 0,\n"
                "        %s_OP_HALT\n"
                "    };\n"
                "    int rc = %s_bc_save(\"_test_roundtrip.bc\", &vm, code, sizeof(code));\n"
                "    TEST_ASSERT(rc == 0, \"bc_save should succeed\");\n"
                "    %s_vm_t vm2;\n"
                "    %s_vm_init(&vm2);\n"
                "    uint8_t *loaded = NULL;\n"
                "    size_t loaded_len = 0;\n"
                "    rc = %s_bc_load(\"_test_roundtrip.bc\", &vm2, &loaded, &loaded_len);\n"
                "    TEST_ASSERT(rc == 0, \"bc_load should succeed\");\n"
                "    TEST_ASSERT(loaded_len == sizeof(code), \"loaded code length should match\");\n"
                "    %s_bc_free_code(loaded);\n"
                "    remove(\"_test_roundtrip.bc\");\n"
                "}\n\n",
                spec->name, spec->name, spec->name,
                NAME, NAME, spec->name, spec->name, spec->name,
                spec->name, spec->name);
        }
    }

    /* === main() === */
    vmgen_buf_appendf(&buf,
        "int main(void) {\n"
        "    printf(\"Running %s smoke tests...\\n\");\n\n"
        "    test_vm_init();\n"
        "    test_push_pop();\n"
        "    test_constants();\n",
        spec->name);

    if (has_basic_ops(spec)) {
        vmgen_buf_append(&buf,
            "    test_halt();\n"
            "    test_push_const();\n");
    }

    if (find_instr(spec, "ADD") >= 0 && has_basic_ops(spec)) {
        vmgen_buf_append(&buf, "    test_add();\n");
    }

    if (find_instr(spec, "SUB") >= 0 && has_basic_ops(spec) &&
        spec->stack.value_type != VMGEN_VALUE_DOUBLE) {
        vmgen_buf_append(&buf, "    test_sub();\n");
    }

    if (has_basic_ops(spec)) {
        vmgen_buf_append(&buf,
            "    test_assembler();\n"
            "    test_disassembler();\n");
    }

    if (spec->features.interned_strings) {
        vmgen_buf_append(&buf, "    test_string_intern();\n");
    }

    if (has_basic_ops(spec)) {
        vmgen_buf_append(&buf, "    test_bytecode_roundtrip();\n");
    }

    vmgen_buf_appendf(&buf,
        "\n    printf(\"%%d tests run, %%d failed\\n\", tests_run, tests_failed);\n"
        "    return tests_failed;\n"
        "}\n");

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
