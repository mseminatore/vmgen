#include "test.h"
#include "util/buffer.h"
#include "codegen/emit.h"
#include "codegen/gen_opcodes.h"
#include "codegen/gen_vm.h"
#include "spec/model.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#define MKDIR(d) mkdir(d, 0755)
#endif

/* Helper: read a file into a malloc'd string */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = (char *)malloc((size_t)len + 1);
    if (!data) { fclose(f); return NULL; }
    fread(data, 1, (size_t)len, f);
    data[len] = '\0';
    fclose(f);
    return data;
}

/* Helper: build a test spec with a few instructions */
static void make_test_spec(vmgen_spec_t *spec) {
    vmgen_spec_init(spec);
    snprintf(spec->name, sizeof(spec->name), "testvm");
    snprintf(spec->version, sizeof(spec->version), "1.0");
    spec->type_flags = VMGEN_TYPE_INT64;

    /* PUSH_CONST index:uint16 */
    snprintf(spec->instructions[0].name, VMGEN_MAX_NAME, "PUSH_CONST");
    spec->instructions[0].operand_count = 1;
    snprintf(spec->instructions[0].operands[0].name, VMGEN_MAX_NAME, "index");
    spec->instructions[0].operands[0].type = VMGEN_OPERAND_UINT16;

    /* ADD (no operands) */
    snprintf(spec->instructions[1].name, VMGEN_MAX_NAME, "ADD");
    spec->instructions[1].operand_count = 0;

    /* HALT (no operands) */
    snprintf(spec->instructions[2].name, VMGEN_MAX_NAME, "HALT");
    spec->instructions[2].operand_count = 0;

    spec->instruction_count = 3;
}

void test_codegen(void) {
    MODULE("Code Generation Utilities");

    /* --- Buffer operations --- */
    SUITE("Buffer");

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    TEST(buf.data == NULL);
    TEST(buf.len == 0);
    TEST(buf.cap == 0);

    vmgen_buf_append(&buf, "hello");
    TEST(buf.len == 5);
    TESTEX("buffer contains 'hello'", strcmp(buf.data, "hello") == 0);

    vmgen_buf_append(&buf, " world");
    TEST(buf.len == 11);
    TESTEX("buffer contains 'hello world'", strcmp(buf.data, "hello world") == 0);

    vmgen_buf_appendc(&buf, '!');
    TEST(buf.len == 12);

    vmgen_buf_free(&buf);
    TEST(buf.data == NULL);
    TEST(buf.len == 0);

    /* --- Format append --- */
    SUITE("Buffer Format");

    vmgen_buf_init(&buf);
    vmgen_buf_appendf(&buf, "value = %d", 42);
    TESTEX("formatted append", strcmp(buf.data, "value = 42") == 0);
    vmgen_buf_free(&buf);

    /* --- Emit helpers --- */
    SUITE("Emit Helpers");

    vmgen_spec_t spec;
    vmgen_spec_init(&spec);
    snprintf(spec.name, sizeof(spec.name), "testvm");
    snprintf(spec.version, sizeof(spec.version), "1.0");

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "test.h", &spec);
    TESTEX("header contains filename", strstr(buf.data, "test.h") != NULL);
    TESTEX("header contains vm name", strstr(buf.data, "testvm") != NULL);
    vmgen_buf_free(&buf);

    vmgen_buf_init(&buf);
    vmgen_emit_guard_open(&buf, "TEST_GUARD_H");
    TESTEX("guard open has #ifndef", strstr(buf.data, "#ifndef TEST_GUARD_H") != NULL);
    TESTEX("guard open has #define", strstr(buf.data, "#define TEST_GUARD_H") != NULL);
    vmgen_emit_guard_close(&buf, "TEST_GUARD_H");
    TESTEX("guard close has #endif", strstr(buf.data, "#endif") != NULL);
    vmgen_buf_free(&buf);

    vmgen_spec_free(&spec);

    /* --- Opcode generation --- */
    MODULE("Opcode Generation");

    SUITE("Generate opcodes.h");

    vmgen_spec_t ospec;
    make_test_spec(&ospec);

    MKDIR("test_output");
    int rc = vmgen_gen_opcodes(&ospec, "test_output");
    TESTEX("gen_opcodes succeeds", rc == 0);

    char *content = read_file("test_output/opcodes.h");
    TESTEX("opcodes.h was created", content != NULL);

    if (content) {
        SUITE("Opcode enum");
        TESTEX("has include guard", strstr(content, "#ifndef TESTVM_OPCODES_H") != NULL);
        TESTEX("has opcode enum", strstr(content, "TESTVM_OP_PUSH_CONST = 0") != NULL);
        TESTEX("has ADD opcode", strstr(content, "TESTVM_OP_ADD = 1") != NULL);
        TESTEX("has HALT opcode", strstr(content, "TESTVM_OP_HALT = 2") != NULL);
        TESTEX("has OP_COUNT", strstr(content, "TESTVM_OP_COUNT = 3") != NULL);

        SUITE("Opcode name table");
        TESTEX("has name table", strstr(content, "testvm_opcode_names") != NULL);
        TESTEX("has PUSH_CONST name", strstr(content, "\"PUSH_CONST\"") != NULL);

        SUITE("Operand count table");
        TESTEX("has operand count table", strstr(content, "testvm_operand_counts") != NULL);

        SUITE("Instruction size table");
        TESTEX("has size table", strstr(content, "testvm_instruction_sizes") != NULL);
        /* PUSH_CONST = 1 opcode + 2 uint16 = 3 bytes */
        TESTEX("PUSH_CONST size is 3", strstr(content, "3,  /* PUSH_CONST */") != NULL);
        /* ADD = 1 opcode byte */
        TESTEX("ADD size is 1", strstr(content, "1,  /* ADD */") != NULL);

        free(content);
    }

    /* --- VM header/source generation --- */
    MODULE("VM Generation");

    SUITE("Generate vm.h and vm.c (switch dispatch)");
    rc = vmgen_gen_vm(&ospec, "test_output");
    TESTEX("gen_vm succeeds", rc == 0);

    content = read_file("test_output/vm.h");
    TESTEX("vm.h was created", content != NULL);

    if (content) {
        TESTEX("vm.h has guard", strstr(content, "#ifndef TESTVM_VM_H") != NULL);
        TESTEX("vm.h has value type", strstr(content, "testvm_value_t") != NULL);
        TESTEX("vm.h has vm struct", strstr(content, "testvm_vm_t") != NULL);
        TESTEX("vm.h has vm_init", strstr(content, "testvm_vm_init") != NULL);
        TESTEX("vm.h has vm_run", strstr(content, "testvm_vm_run") != NULL);
        TESTEX("vm.h has vm_push", strstr(content, "testvm_vm_push") != NULL);
        TESTEX("vm.h has vm_pop", strstr(content, "testvm_vm_pop") != NULL);
        TESTEX("vm.h has stack array", strstr(content, "stack[") != NULL);
        TESTEX("vm.h includes opcodes.h", strstr(content, "#include \"opcodes.h\"") != NULL);
        TESTEX("vm.h has const pool struct", strstr(content, "testvm_const_pool_t") != NULL);
        TESTEX("vm.h has MAX_CONSTANTS", strstr(content, "TESTVM_MAX_CONSTANTS") != NULL);
        TESTEX("vm.h has add_constant proto", strstr(content, "testvm_vm_add_constant") != NULL);
        TESTEX("vm.h has constants in vm struct", strstr(content, "constants") != NULL);
        free(content);
    }

    content = read_file("test_output/vm.c");
    TESTEX("vm.c was created", content != NULL);

    if (content) {
        TESTEX("vm.c has switch dispatch", strstr(content, "switch (opcode)") != NULL);
        TESTEX("vm.c has PUSH_CONST case", strstr(content, "TESTVM_OP_PUSH_CONST") != NULL);
        TESTEX("vm.c has HALT case", strstr(content, "TESTVM_OP_HALT") != NULL);
        TESTEX("vm.c has push function", strstr(content, "testvm_vm_push") != NULL);
        TESTEX("vm.c has pop function", strstr(content, "testvm_vm_pop") != NULL);
        TESTEX("vm.c has stack overflow check", strstr(content, "stack overflow") != NULL);
        TESTEX("vm.c has stack underflow check", strstr(content, "stack underflow") != NULL);
        TESTEX("vm.c has READ_UINT16 macro", strstr(content, "READ_UINT16") != NULL);
        TESTEX("vm.c has add_constant", strstr(content, "testvm_vm_add_constant") != NULL);
        TESTEX("vm.c has constant pool overflow", strstr(content, "constant pool overflow") != NULL);
        free(content);
    }

    /* --- fnptr dispatch --- */
    SUITE("Generate vm.c (fnptr dispatch)");
    ospec.dispatch = VMGEN_DISPATCH_FNPTR;
    rc = vmgen_gen_vm(&ospec, "test_output");
    TESTEX("gen_vm fnptr succeeds", rc == 0);

    content = read_file("test_output/vm.c");
    TESTEX("vm.c fnptr was created", content != NULL);
    if (content) {
        TESTEX("vm.c has dispatch table", strstr(content, "testvm_dispatch_table") != NULL);
        TESTEX("vm.c has handler functions", strstr(content, "handle_PUSH_CONST") != NULL);
        TESTEX("vm.c has handler type", strstr(content, "testvm_handler_t") != NULL);
        free(content);
    }

    /* Cleanup */
    remove("test_output/opcodes.h");
    remove("test_output/vm.h");
    remove("test_output/vm.c");

    vmgen_spec_free(&ospec);
}
