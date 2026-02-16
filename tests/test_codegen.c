#include "test.h"
#include "util/buffer.h"
#include "codegen/emit.h"
#include "codegen/gen_opcodes.h"
#include "codegen/gen_vm.h"
#include "codegen/gen_assembler.h"
#include "codegen/gen_disasm.h"
#include "codegen/gen_closures.h"
#include "codegen/gen_modules.h"
#include "codegen/gen_fibers.h"
#include "codegen/gen_native.h"
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

    /* --- Disassembler generation --- */
    MODULE("Disassembler Generation");

    SUITE("Generate disasm.h and disasm.c");
    ospec.dispatch = VMGEN_DISPATCH_SWITCH;
    rc = vmgen_gen_disasm(&ospec, "test_output");
    TESTEX("gen_disasm succeeds", rc == 0);

    content = read_file("test_output/disasm.h");
    TESTEX("disasm.h created", content != NULL);
    if (content) {
        TESTEX("disasm.h has guard", strstr(content, "#ifndef TESTVM_DISASM_H") != NULL);
        TESTEX("disasm.h has disasm_instruction", strstr(content, "testvm_disasm_instruction") != NULL);
        TESTEX("disasm.h has disasm", strstr(content, "testvm_disasm") != NULL);
        free(content);
    }

    content = read_file("test_output/disasm.c");
    TESTEX("disasm.c created", content != NULL);
    if (content) {
        TESTEX("disasm.c includes opcodes.h", strstr(content, "#include \"opcodes.h\"") != NULL);
        TESTEX("disasm.c has switch on opcode", strstr(content, "switch (opcode)") != NULL);
        TESTEX("disasm.c has PUSH_CONST case", strstr(content, "TESTVM_OP_PUSH_CONST") != NULL);
        TESTEX("disasm.c has full disasm func", strstr(content, "testvm_disasm(") != NULL);
        free(content);
    }

    /* --- Assembler generation --- */
    MODULE("Assembler Generation");

    SUITE("Generate assembler.h and assembler.c");
    rc = vmgen_gen_assembler(&ospec, "test_output");
    TESTEX("gen_assembler succeeds", rc == 0);

    content = read_file("test_output/assembler.h");
    TESTEX("assembler.h created", content != NULL);
    if (content) {
        TESTEX("asm.h has guard", strstr(content, "#ifndef TESTVM_ASSEMBLER_H") != NULL);
        TESTEX("asm.h has assembler struct", strstr(content, "testvm_assembler_t") != NULL);
        TESTEX("asm.h has asm_init", strstr(content, "testvm_asm_init") != NULL);
        TESTEX("asm.h has asm_assemble", strstr(content, "testvm_asm_assemble") != NULL);
        TESTEX("asm.h has emit_byte", strstr(content, "testvm_asm_emit_byte") != NULL);
        free(content);
    }

    content = read_file("test_output/assembler.c");
    TESTEX("assembler.c created", content != NULL);
    if (content) {
        TESTEX("asm.c has mnemonic matching", strstr(content, "strcmp(mnemonic,") != NULL);
        TESTEX("asm.c handles PUSH_CONST", strstr(content, "\"PUSH_CONST\"") != NULL);
        TESTEX("asm.c handles HALT", strstr(content, "\"HALT\"") != NULL);
        TESTEX("asm.c has assemble function", strstr(content, "testvm_asm_assemble(") != NULL);
        TESTEX("asm.c has emit_u16", strstr(content, "testvm_asm_emit_u16") != NULL);
        TESTEX("asm.c handles unknown mnemonic", strstr(content, "unknown mnemonic") != NULL);
        free(content);
    }

    /* --- Native function generation --- */
    MODULE("Native Function Generation");

    SUITE("Generate native.h and native.c");
    ospec.dispatch = VMGEN_DISPATCH_SWITCH;
    rc = vmgen_gen_native(&ospec, "test_output");
    TESTEX("gen_native succeeds", rc == 0);

    content = read_file("test_output/native.h");
    TESTEX("native.h created", content != NULL);
    if (content) {
        TESTEX("native.h has guard", strstr(content, "#ifndef TESTVM_NATIVE_H") != NULL);
        TESTEX("native.h has fn typedef", strstr(content, "testvm_native_fn_t") != NULL);
        TESTEX("native.h has entry struct", strstr(content, "testvm_native_entry_t") != NULL);
        TESTEX("native.h has table struct", strstr(content, "testvm_native_table_t") != NULL);
        TESTEX("native.h has register", strstr(content, "testvm_native_register") != NULL);
        TESTEX("native.h has find", strstr(content, "testvm_native_find") != NULL);
        TESTEX("native.h has call", strstr(content, "testvm_native_call") != NULL);
        free(content);
    }

    content = read_file("test_output/native.c");
    TESTEX("native.c created", content != NULL);
    if (content) {
        TESTEX("native.c includes native.h", strstr(content, "#include \"native.h\"") != NULL);
        TESTEX("native.c has table_init", strstr(content, "testvm_native_table_init") != NULL);
        TESTEX("native.c has table_free", strstr(content, "testvm_native_table_free") != NULL);
        TESTEX("native.c has register impl", strstr(content, "table->count++") != NULL);
        TESTEX("native.c has find with strcmp", strstr(content, "strcmp(table->entries") != NULL);
        TESTEX("native.c has call dispatch", strstr(content, "entries[index].fn(vm") != NULL);
        free(content);
    }

    /* --- Closure generation --- */
    MODULE("Closure Generation");

    SUITE("Closures disabled skips gen");
    {
        vmgen_spec_t cspec;
        make_test_spec(&cspec);
        cspec.features.closures = false;
        rc = vmgen_gen_closures(&cspec, "test_output");
        TESTEX("gen_closures returns 0 when disabled", rc == 0);
        vmgen_spec_free(&cspec);
    }

    SUITE("Generate closures.h and closures.c");
    {
        vmgen_spec_t cspec;
        make_test_spec(&cspec);
        cspec.features.closures = true;
        rc = vmgen_gen_closures(&cspec, "test_output");
        TESTEX("gen_closures succeeds", rc == 0);

        content = read_file("test_output/closures.h");
        TESTEX("closures.h created", content != NULL);
        if (content) {
            TESTEX("closures.h has guard", strstr(content, "#ifndef TESTVM_CLOSURES_H") != NULL);
            TESTEX("closures.h has upvalue_t", strstr(content, "testvm_upvalue_t") != NULL);
            TESTEX("closures.h has closure_t", strstr(content, "testvm_closure_t") != NULL);
            TESTEX("closures.h has is_closed", strstr(content, "is_closed") != NULL);
            TESTEX("closures.h has init_closure", strstr(content, "testvm_init_closure") != NULL);
            TESTEX("closures.h has get_upvalue", strstr(content, "testvm_get_upvalue") != NULL);
            TESTEX("closures.h has set_upvalue", strstr(content, "testvm_set_upvalue") != NULL);
            TESTEX("closures.h has close_upvalue", strstr(content, "testvm_close_upvalue") != NULL);
            free(content);
        }

        content = read_file("test_output/closures.c");
        TESTEX("closures.c created", content != NULL);
        if (content) {
            TESTEX("closures.c includes closures.h", strstr(content, "#include \"closures.h\"") != NULL);
            TESTEX("closures.c has init_closure impl", strstr(content, "testvm_init_closure") != NULL);
            TESTEX("closures.c has free_closure impl", strstr(content, "testvm_free_closure") != NULL);
            TESTEX("closures.c has upvalue alloc", strstr(content, "calloc") != NULL);
            TESTEX("closures.c has close logic", strstr(content, "upvalue->location = &upvalue->closed") != NULL);
            free(content);
        }

        vmgen_spec_free(&cspec);
    }

    /* --- Module generation --- */
    MODULE("Module Generation");

    SUITE("Modules disabled skips gen");
    {
        vmgen_spec_t mspec;
        make_test_spec(&mspec);
        mspec.features.modules_enabled = false;
        rc = vmgen_gen_modules(&mspec, "test_output");
        TESTEX("gen_modules returns 0 when disabled", rc == 0);
        vmgen_spec_free(&mspec);
    }

    SUITE("Generate modules.h and modules.c");
    {
        vmgen_spec_t mspec;
        make_test_spec(&mspec);
        mspec.features.modules_enabled = true;
        mspec.features.module_loading = VMGEN_MODULE_DYNAMIC;
        rc = vmgen_gen_modules(&mspec, "test_output");
        TESTEX("gen_modules succeeds", rc == 0);

        content = read_file("test_output/modules.h");
        TESTEX("modules.h created", content != NULL);
        if (content) {
            TESTEX("modules.h has guard", strstr(content, "#ifndef TESTVM_MODULES_H") != NULL);
            TESTEX("modules.h has state enum", strstr(content, "TESTVM_MODULE_LOADED") != NULL);
            TESTEX("modules.h has module_t", strstr(content, "testvm_module_t") != NULL);
            TESTEX("modules.h has table_t", strstr(content, "testvm_module_table_t") != NULL);
            TESTEX("modules.h has module_load", strstr(content, "testvm_module_load") != NULL);
            TESTEX("modules.h has module_find", strstr(content, "testvm_module_find") != NULL);
            TESTEX("modules.h has globals", strstr(content, "MODULE_MAX_GLOBALS") != NULL);
            free(content);
        }

        content = read_file("test_output/modules.c");
        TESTEX("modules.c created", content != NULL);
        if (content) {
            TESTEX("modules.c includes modules.h", strstr(content, "#include \"modules.h\"") != NULL);
            TESTEX("modules.c has table_init", strstr(content, "testvm_module_table_init") != NULL);
            TESTEX("modules.c has table_free", strstr(content, "testvm_module_table_free") != NULL);
            TESTEX("modules.c has load impl", strstr(content, "testvm_module_load") != NULL);
            TESTEX("modules.c has find impl", strstr(content, "testvm_module_find") != NULL);
            TESTEX("modules.c has dynamic comment", strstr(content, "Dynamic loading") != NULL);
            free(content);
        }

        vmgen_spec_free(&mspec);
    }

    /* --- Fiber generation --- */
    MODULE("Fiber Generation");

    SUITE("Fibers disabled skips gen");
    {
        vmgen_spec_t fspec;
        make_test_spec(&fspec);
        fspec.features.coroutines = false;
        rc = vmgen_gen_fibers(&fspec, "test_output");
        TESTEX("gen_fibers returns 0 when disabled", rc == 0);
        vmgen_spec_free(&fspec);
    }

    SUITE("Generate fibers.h and fibers.c");
    {
        vmgen_spec_t fspec;
        make_test_spec(&fspec);
        fspec.features.coroutines = true;
        rc = vmgen_gen_fibers(&fspec, "test_output");
        TESTEX("gen_fibers succeeds", rc == 0);

        content = read_file("test_output/fibers.h");
        TESTEX("fibers.h created", content != NULL);
        if (content) {
            TESTEX("fibers.h has guard", strstr(content, "#ifndef TESTVM_FIBERS_H") != NULL);
            TESTEX("fibers.h has status enum", strstr(content, "TESTVM_FIBER_CREATED") != NULL);
            TESTEX("fibers.h has SUSPENDED", strstr(content, "TESTVM_FIBER_SUSPENDED") != NULL);
            TESTEX("fibers.h has fiber_t", strstr(content, "testvm_fiber_t") != NULL);
            TESTEX("fibers.h has fiber_new", strstr(content, "testvm_fiber_new") != NULL);
            TESTEX("fibers.h has fiber_resume", strstr(content, "testvm_fiber_resume") != NULL);
            TESTEX("fibers.h has fiber_yield", strstr(content, "testvm_fiber_yield") != NULL);
            TESTEX("fibers.h has fiber_status", strstr(content, "testvm_fiber_status") != NULL);
            free(content);
        }

        content = read_file("test_output/fibers.c");
        TESTEX("fibers.c created", content != NULL);
        if (content) {
            TESTEX("fibers.c includes fibers.h", strstr(content, "#include \"fibers.h\"") != NULL);
            TESTEX("fibers.c has fiber_new impl", strstr(content, "testvm_fiber_new") != NULL);
            TESTEX("fibers.c has malloc", strstr(content, "malloc(sizeof") != NULL);
            TESTEX("fibers.c has fiber_free impl", strstr(content, "testvm_fiber_free") != NULL);
            TESTEX("fibers.c has status check in resume", strstr(content, "FIBER_DEAD") != NULL);
            TESTEX("fibers.c has yield sets SUSPENDED", strstr(content, "FIBER_SUSPENDED") != NULL);
            free(content);
        }

        vmgen_spec_free(&fspec);
    }

    /* Cleanup */
    remove("test_output/opcodes.h");
    remove("test_output/vm.h");
    remove("test_output/vm.c");
    remove("test_output/disasm.h");
    remove("test_output/disasm.c");
    remove("test_output/assembler.h");
    remove("test_output/assembler.c");
    remove("test_output/native.h");
    remove("test_output/native.c");
    remove("test_output/closures.h");
    remove("test_output/closures.c");
    remove("test_output/modules.h");
    remove("test_output/modules.c");
    remove("test_output/fibers.h");
    remove("test_output/fibers.c");

    vmgen_spec_free(&ospec);
}
