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
#include "codegen/gen_strings.h"
#include "codegen/gen_gc.h"
#include "codegen/gen_build.h"
#include "codegen/gen_bytecode.h"
#include "codegen/gen_test.h"
#include "codegen/gen_single_header.h"
#include "codegen/action_parser.h"
#include "codegen/action_emit.h"
#include "spec/model.h"
#include "spec/parser.h"
#include "spec/validate.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _MSC_VER
#define vmgen_strdup _strdup
#else
#define vmgen_strdup strdup
#endif

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
        TESTEX("disasm.h has disasm_instruction_buf", strstr(content, "testvm_disasm_instruction_buf") != NULL);
        TESTEX("disasm.h has disasm_buf", strstr(content, "testvm_disasm_buf") != NULL);
        free(content);
    }

    content = read_file("test_output/disasm.c");
    TESTEX("disasm.c created", content != NULL);
    if (content) {
        TESTEX("disasm.c includes opcodes.h", strstr(content, "#include \"opcodes.h\"") != NULL);
        TESTEX("disasm.c has switch on opcode", strstr(content, "switch (opcode)") != NULL);
        TESTEX("disasm.c has PUSH_CONST case", strstr(content, "TESTVM_OP_PUSH_CONST") != NULL);
        TESTEX("disasm.c has full disasm func", strstr(content, "testvm_disasm(") != NULL);
        TESTEX("disasm.c has snprintf buf variant", strstr(content, "testvm_disasm_instruction_buf") != NULL);
        TESTEX("disasm.c has disasm_buf func", strstr(content, "testvm_disasm_buf") != NULL);
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
            TESTEX("modules.c has circular import check", strstr(content, "circular import") != NULL);
            TESTEX("modules.c sets LOADING state", strstr(content, "MODULE_LOADING") != NULL);
            TESTEX("modules.c has mark_loaded", strstr(content, "testvm_module_mark_loaded") != NULL);
            free(content);
        }

        content = read_file("test_output/modules.h");
        if (content) {
            TESTEX("modules.h has mark_loaded decl", strstr(content, "module_mark_loaded") != NULL);
            TESTEX("modules.h has LOADING state", strstr(content, "MODULE_LOADING") != NULL);
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

/* Integration test: full pipeline with full.yaml */
void test_integration(void) {
    MODULE("Integration - Full Pipeline");

    MKDIR("test_gen");

    SUITE("Parse and validate full.yaml");
    vmgen_spec_t spec;
    vmgen_spec_init(&spec);
    int rc = vmgen_parse_spec("specs/full.yaml", &spec);
    TESTEX("parse full.yaml succeeds", rc == 0);
    rc = vmgen_validate_spec(&spec);
    TESTEX("validate full.yaml succeeds", rc == 0);

    SUITE("Generate all files from full.yaml");
    TESTEX("gen_opcodes", vmgen_gen_opcodes(&spec, "test_gen") == 0);
    TESTEX("gen_vm", vmgen_gen_vm(&spec, "test_gen") == 0);
    TESTEX("gen_assembler", vmgen_gen_assembler(&spec, "test_gen") == 0);
    TESTEX("gen_disasm", vmgen_gen_disasm(&spec, "test_gen") == 0);
    TESTEX("gen_strings", vmgen_gen_strings(&spec, "test_gen") == 0);
    TESTEX("gen_gc", vmgen_gen_gc(&spec, "test_gen") == 0);
    TESTEX("gen_native", vmgen_gen_native(&spec, "test_gen") == 0);
    TESTEX("gen_closures", vmgen_gen_closures(&spec, "test_gen") == 0);
    TESTEX("gen_modules", vmgen_gen_modules(&spec, "test_gen") == 0);
    TESTEX("gen_fibers", vmgen_gen_fibers(&spec, "test_gen") == 0);
    TESTEX("gen_bytecode", vmgen_gen_bytecode(&spec, "test_gen") == 0);
    TESTEX("gen_test", vmgen_gen_test(&spec, "test_gen") == 0);
    TESTEX("gen_build", vmgen_gen_build(&spec, "test_gen") == 0);

    SUITE("Verify all output files exist");
    char *content;
    content = read_file("test_gen/opcodes.h");
    TESTEX("opcodes.h exists", content != NULL);
    if (content) { TESTEX("opcodes.h has fullvm prefix", strstr(content, "FULLVM_OP_") != NULL); free(content); }

    content = read_file("test_gen/vm.h");
    TESTEX("vm.h exists", content != NULL);
    if (content) { TESTEX("vm.h has fullvm struct", strstr(content, "fullvm_vm_t") != NULL); free(content); }

    content = read_file("test_gen/vm.c");
    TESTEX("vm.c exists", content != NULL);
    if (content) { TESTEX("vm.c has dispatch table (fnptr)", strstr(content, "fullvm_dispatch_table") != NULL); free(content); }

    content = read_file("test_gen/strings.h");
    TESTEX("strings.h exists", content != NULL);
    if (content) free(content);

    content = read_file("test_gen/gc.h");
    TESTEX("gc.h exists", content != NULL);
    if (content) free(content);

    content = read_file("test_gen/closures.h");
    TESTEX("closures.h exists", content != NULL);
    if (content) { TESTEX("closures has fullvm upvalue", strstr(content, "fullvm_upvalue_t") != NULL); free(content); }

    content = read_file("test_gen/modules.h");
    TESTEX("modules.h exists", content != NULL);
    if (content) { TESTEX("modules has fullvm module", strstr(content, "fullvm_module_t") != NULL); free(content); }

    content = read_file("test_gen/fibers.h");
    TESTEX("fibers.h exists", content != NULL);
    if (content) { TESTEX("fibers has fullvm fiber", strstr(content, "fullvm_fiber_t") != NULL); free(content); }

    content = read_file("test_gen/bytecode.h");
    TESTEX("bytecode.h exists", content != NULL);
    if (content) {
        TESTEX("bytecode.h has magic", strstr(content, "BC_MAGIC") != NULL);
        TESTEX("bytecode.h has bc_save", strstr(content, "bc_save") != NULL);
        TESTEX("bytecode.h has bc_load", strstr(content, "bc_load") != NULL);
        free(content);
    }

    content = read_file("test_gen/test_vm.c");
    TESTEX("test_vm.c exists", content != NULL);
    if (content) {
        TESTEX("test_vm.c includes vm.h", strstr(content, "#include \"vm.h\"") != NULL);
        TESTEX("test_vm.c has test_vm_init", strstr(content, "test_vm_init") != NULL);
        TESTEX("test_vm.c has test_push_pop", strstr(content, "test_push_pop") != NULL);
        TESTEX("test_vm.c has test_halt", strstr(content, "test_halt") != NULL);
        TESTEX("test_vm.c has test_add", strstr(content, "test_add") != NULL);
        TESTEX("test_vm.c has test_assembler", strstr(content, "test_assembler") != NULL);
        TESTEX("test_vm.c has test_bytecode_roundtrip", strstr(content, "test_bytecode_roundtrip") != NULL);
        TESTEX("test_vm.c has main", strstr(content, "int main(void)") != NULL);
        free(content);
    }

    content = read_file("test_gen/CMakeLists.txt");
    TESTEX("CMakeLists.txt exists", content != NULL);
    if (content) {
        TESTEX("CMakeLists includes test_vm.c", strstr(content, "test_vm.c") != NULL);
        TESTEX("CMakeLists has enable_testing", strstr(content, "enable_testing") != NULL);
        TESTEX("CMakeLists has add_test", strstr(content, "add_test") != NULL);
        free(content);
    }

    /* Cleanup */
    remove("test_gen/opcodes.h");
    remove("test_gen/vm.h");
    remove("test_gen/vm.c");
    remove("test_gen/assembler.h");
    remove("test_gen/assembler.c");
    remove("test_gen/disasm.h");
    remove("test_gen/disasm.c");
    remove("test_gen/strings.h");
    remove("test_gen/strings.c");
    remove("test_gen/gc.h");
    remove("test_gen/gc.c");
    remove("test_gen/native.h");
    remove("test_gen/native.c");
    remove("test_gen/closures.h");
    remove("test_gen/closures.c");
    remove("test_gen/modules.h");
    remove("test_gen/modules.c");
    remove("test_gen/fibers.h");
    remove("test_gen/fibers.c");
    remove("test_gen/bytecode.h");
    remove("test_gen/bytecode.c");
    remove("test_gen/test_vm.c");
    remove("test_gen/CMakeLists.txt");

    vmgen_spec_free(&spec);
}

/* Action DSL parser and codegen tests */
void test_actions(void) {
    MODULE("Action DSL Parser");

    SUITE("Parse basic pop/push");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("pop b; pop a; push a + b", &action);
        TESTEX("parse ADD action succeeds", rc == 0);
        TESTEX("ADD has 3 statements", action.count == 3);
        TESTEX("stmt 0 is POP", action.stmts[0].type == ACT_POP);
        TESTEX("stmt 0 ident is 'b'", strcmp(action.stmts[0].ident, "b") == 0);
        TESTEX("stmt 1 is POP", action.stmts[1].type == ACT_POP);
        TESTEX("stmt 1 ident is 'a'", strcmp(action.stmts[1].ident, "a") == 0);
        TESTEX("stmt 2 is PUSH", action.stmts[2].type == ACT_PUSH);
        TESTEX("stmt 2 has expr", action.stmts[2].expr != NULL);
        vmgen_action_free(&action);
    }

    SUITE("Parse operand references");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("push constants[$index]", &action);
        TESTEX("parse constants ref succeeds", rc == 0);
        TESTEX("has 1 statement", action.count == 1);
        TESTEX("stmt is PUSH", action.stmts[0].type == ACT_PUSH);
        TESTEX("expr is CONST_INDEX", action.stmts[0].expr->type == EXPR_CONST_INDEX);
        vmgen_action_free(&action);
    }

    SUITE("Parse peek");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("peek a; push a", &action);
        TESTEX("parse peek succeeds", rc == 0);
        TESTEX("has 2 statements", action.count == 2);
        TESTEX("stmt 0 is PEEK", action.stmts[0].type == ACT_PEEK);
        TESTEX("stmt 1 is PUSH", action.stmts[1].type == ACT_PUSH);
        vmgen_action_free(&action);
    }

    SUITE("Parse jump");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("jump $offset", &action);
        TESTEX("parse jump succeeds", rc == 0);
        TESTEX("has 1 statement", action.count == 1);
        TESTEX("stmt is JUMP", action.stmts[0].type == ACT_JUMP);
        vmgen_action_free(&action);
    }

    SUITE("Parse jump_if_zero");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("pop a; jump_if_zero a $offset", &action);
        TESTEX("parse jump_if_zero succeeds", rc == 0);
        TESTEX("has 2 statements", action.count == 2);
        TESTEX("stmt 1 is JUMP_IF_ZERO", action.stmts[1].type == ACT_JUMP_IF_ZERO);
        TESTEX("stmt 1 ident is 'a'", strcmp(action.stmts[1].ident, "a") == 0);
        vmgen_action_free(&action);
    }

    SUITE("Parse halt and return");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("halt", &action);
        TESTEX("parse halt succeeds", rc == 0);
        TESTEX("stmt is HALT", action.stmts[0].type == ACT_HALT);
        vmgen_action_free(&action);

        rc = vmgen_action_parse("return", &action);
        TESTEX("parse return succeeds", rc == 0);
        TESTEX("stmt is RETURN", action.stmts[0].type == ACT_RETURN);
        vmgen_action_free(&action);
    }

    SUITE("Parse unary expressions");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("pop a; push -a", &action);
        TESTEX("parse negate succeeds", rc == 0);
        TESTEX("push expr is UNARY", action.stmts[1].expr->type == EXPR_UNARY);
        vmgen_action_free(&action);
    }

    SUITE("Parse comparison operators");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("pop b; pop a; push a == b", &action);
        TESTEX("parse EQ succeeds", rc == 0);
        TESTEX("push expr is BINOP", action.stmts[2].expr->type == EXPR_BINOP);
        vmgen_action_free(&action);

        rc = vmgen_action_parse("pop b; pop a; push a < b", &action);
        TESTEX("parse LT succeeds", rc == 0);
        vmgen_action_free(&action);
    }

    SUITE("Parse errors");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("badkeyword x", &action);
        TESTEX("unknown keyword fails", rc == -1);
        TESTEX("error message set", action.error[0] != '\0');
        vmgen_action_free(&action);
    }

    SUITE("Stack effect calculation");
    {
        vmgen_action_t action;
        vmgen_action_parse("pop b; pop a; push a + b", &action);
        TESTEX("ADD effect is -1", vmgen_action_stack_effect(&action) == -1);
        vmgen_action_free(&action);

        vmgen_action_parse("push constants[$index]", &action);
        TESTEX("PUSH_CONST effect is +1", vmgen_action_stack_effect(&action) == 1);
        vmgen_action_free(&action);

        vmgen_action_parse("pop _", &action);
        TESTEX("POP effect is -1", vmgen_action_stack_effect(&action) == -1);
        vmgen_action_free(&action);

        vmgen_action_parse("peek a; push a", &action);
        TESTEX("DUP effect is +1", vmgen_action_stack_effect(&action) == 1);
        vmgen_action_free(&action);
    }

    SUITE("Empty action");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("", &action);
        TESTEX("empty parses ok", rc == 0);
        TESTEX("empty has 0 stmts", action.count == 0);
        vmgen_action_free(&action);
    }

    /* --- Action codegen --- */
    MODULE("Action Code Emission");

    SUITE("Emit DSL action to C");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_INT64;

        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));
        snprintf(instr.name, VMGEN_MAX_NAME, "ADD");

        vmgen_action_t action;
        vmgen_action_parse("pop b; pop a; push a + b", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);

        TESTEX("emits vm_pop for b", strstr(buf.data, "test_vm_pop(vm)") != NULL);
        TESTEX("emits vm_push", strstr(buf.data, "test_vm_push(vm,") != NULL);
        TESTEX("emits a + b", strstr(buf.data, "(a + b)") != NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("Emit inline C");
    {
        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit_inline_c(&buf, "    ", "int x = 42;\nprintf(\"%d\", x);");
        TESTEX("inline C has first line", strstr(buf.data, "    int x = 42;\n") != NULL);
        TESTEX("inline C has second line", strstr(buf.data, "    printf(\"%d\", x);") != NULL);
        vmgen_buf_free(&buf);
    }

    SUITE("Emit constants reference");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        vmgen_action_t action;
        vmgen_action_parse("push constants[$index]", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);
        TESTEX("emits constants.values", strstr(buf.data, "vm->constants.values[index]") != NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    /* --- Generated code verification --- */
    MODULE("Action Integration in Generated Code");

    SUITE("Generated vm.c with DSL actions");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        /* Add action to ADD instruction */
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm with action succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has vm_pop for ADD", strstr(content, "testvm_vm_pop(vm)") != NULL);
            TESTEX("vm.c has vm_push for ADD", strstr(content, "testvm_vm_push(vm,") != NULL);
            TESTEX("vm.c still has TODO for PUSH_CONST", strstr(content, "TODO: implement PUSH_CONST") != NULL);
            TESTEX("vm.c no TODO for ADD", strstr(content, "TODO: implement ADD") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Generated vm.c with inline C action");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.instructions[1].action_c = vmgen_strdup("int64_t b = testvm_vm_pop(vm);\nint64_t a = testvm_vm_pop(vm);\ntestvm_vm_push(vm, a + b);");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm with inline C succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has inline C code", strstr(content, "testvm_vm_push(vm, a + b)") != NULL);
            TESTEX("vm.c no TODO for ADD", strstr(content, "TODO: implement ADD") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Parse minimal.yaml with actions");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        int rc = vmgen_parse_spec("specs/minimal.yaml", &spec);
        TESTEX("parse minimal.yaml succeeds", rc == 0);
        TESTEX("ADD has action", spec.instructions[2].action != NULL);
        if (spec.instructions[2].action) {
            TESTEX("ADD action is pop/push", strstr(spec.instructions[2].action, "pop") != NULL);
        }
        TESTEX("ADD stack_effect_set", spec.instructions[2].stack_effect_set == true);
        TESTEX("ADD stack_effect is -1", spec.instructions[2].stack_effect == -1);
        vmgen_spec_free(&spec);
    }

    /* --- GC safety --- */
    MODULE("GC Safety in Generated Code");

    SUITE("GC-enabled spec has inhibit wrapping");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm with gc succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has gc_inhibit", strstr(content, "testvm_gc_inhibit") != NULL);
            TESTEX("vm.c has gc_allow", strstr(content, "testvm_gc_allow") != NULL);
            free(content);
        }

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h includes gc.h", strstr(hdr, "#include \"gc.h\"") != NULL);
            TESTEX("vm.h has gc pointer", strstr(hdr, "testvm_gc_t *gc") != NULL);
            free(hdr);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("No-GC spec has no inhibit wrapping");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = false;
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm without gc succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has no gc_inhibit", strstr(content, "gc_inhibit") == NULL);
            TESTEX("vm.c has no gc_allow", strstr(content, "gc_allow") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("GC struct has inhibit_count");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;

        MKDIR("test_output");
        int rc = vmgen_gen_gc(&spec, "test_output");
        TESTEX("gen_gc succeeds", rc == 0);

        char *content = read_file("test_output/gc.h");
        TESTEX("gc.h created", content != NULL);
        if (content) {
            TESTEX("gc.h has inhibit_count", strstr(content, "inhibit_count") != NULL);
            TESTEX("gc.h has gc_inhibit decl", strstr(content, "testvm_gc_inhibit") != NULL);
            TESTEX("gc.h has gc_allow decl", strstr(content, "testvm_gc_allow") != NULL);
            free(content);
        }

        char *impl = read_file("test_output/gc.c");
        TESTEX("gc.c created", impl != NULL);
        if (impl) {
            TESTEX("gc.c checks inhibit in alloc", strstr(impl, "inhibit_count == 0") != NULL);
            TESTEX("gc.c checks inhibit in collect", strstr(impl, "inhibit_count > 0") != NULL);
            free(impl);
        }

        remove("test_output/gc.h");
        remove("test_output/gc.c");
        vmgen_spec_free(&spec);
    }

    SUITE("GC has mark_roots callback");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;

        MKDIR("test_output");
        int rc = vmgen_gen_gc(&spec, "test_output");
        TESTEX("gen_gc succeeds", rc == 0);

        char *hdr = read_file("test_output/gc.h");
        TESTEX("gc.h created", hdr != NULL);
        if (hdr) {
            TESTEX("gc.h has mark_roots_fn typedef", strstr(hdr, "mark_roots_fn") != NULL);
            TESTEX("gc.h has mark_roots field", strstr(hdr, "mark_roots;") != NULL);
            TESTEX("gc.h has roots_data field", strstr(hdr, "roots_data") != NULL);
            TESTEX("gc.h has gc_set_roots decl", strstr(hdr, "gc_set_roots") != NULL);
            free(hdr);
        }

        char *impl = read_file("test_output/gc.c");
        TESTEX("gc.c created", impl != NULL);
        if (impl) {
            TESTEX("gc.c calls mark_roots in collect", strstr(impl, "gc->mark_roots(gc->roots_data)") != NULL);
            TESTEX("gc.c has gc_set_roots impl", strstr(impl, "gc->mark_roots = fn") != NULL);
            TESTEX("gc.c inits mark_roots NULL", strstr(impl, "mark_roots = NULL") != NULL);
            free(impl);
        }

        remove("test_output/gc.h");
        remove("test_output/gc.c");
        vmgen_spec_free(&spec);
    }

    SUITE("GC-enabled VM has mark_roots + OBJ type");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has VAL_OBJ enum", strstr(hdr, "VAL_OBJ") != NULL);
            TESTEX("vm.h has obj pointer in union", strstr(hdr, "obj_t *obj") != NULL);
            TESTEX("vm.h has OBJ constructor", strstr(hdr, "_OBJ(v)") != NULL);
            free(hdr);
        }

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has mark_roots function", strstr(content, "mark_roots") != NULL);
            TESTEX("vm.c scans stack for OBJ", strstr(content, "VAL_OBJ") != NULL);
            TESTEX("vm.c calls gc_mark_object", strstr(content, "gc_mark_object") != NULL);
            TESTEX("vm.c registers roots in run", strstr(content, "gc_set_roots") != NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("No-GC VM has no OBJ type or mark_roots");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = false;
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has no VAL_OBJ", strstr(hdr, "VAL_OBJ") == NULL);
            TESTEX("vm.h has no obj pointer", strstr(hdr, "obj_t *obj") == NULL);
            free(hdr);
        }

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has no mark_roots", strstr(content, "mark_roots") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    /* --- GC integration: strings --- */
    MODULE("GC Integration - Strings");

    SUITE("GC-enabled strings have obj header + gc_alloc");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;
        spec.features.interned_strings = true;

        MKDIR("test_output");
        int rc = vmgen_gen_strings(&spec, "test_output");
        TESTEX("gen_strings succeeds", rc == 0);

        char *hdr = read_file("test_output/strings.h");
        TESTEX("strings.h created", hdr != NULL);
        if (hdr) {
            TESTEX("strings.h includes gc.h", strstr(hdr, "#include \"gc.h\"") != NULL);
            TESTEX("strings.h has obj field", strstr(hdr, "obj_t obj") != NULL);
            TESTEX("strings.h has gc pointer in table", strstr(hdr, "gc_t *gc") != NULL);
            TESTEX("strings.h has string_table_mark", strstr(hdr, "string_table_mark") != NULL);
            free(hdr);
        }

        char *impl = read_file("test_output/strings.c");
        TESTEX("strings.c created", impl != NULL);
        if (impl) {
            TESTEX("strings.c uses gc_alloc", strstr(impl, "gc_alloc") != NULL);
            TESTEX("strings.c has mark function", strstr(impl, "string_table_mark") != NULL);
            TESTEX("strings.c marks objects", strstr(impl, "gc_mark_object") != NULL);
            free(impl);
        }

        remove("test_output/strings.h");
        remove("test_output/strings.c");
        vmgen_spec_free(&spec);
    }

    SUITE("No-GC strings use plain malloc");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = false;
        spec.features.interned_strings = true;

        MKDIR("test_output");
        int rc = vmgen_gen_strings(&spec, "test_output");
        TESTEX("gen_strings succeeds", rc == 0);

        char *hdr = read_file("test_output/strings.h");
        TESTEX("strings.h created", hdr != NULL);
        if (hdr) {
            TESTEX("no gc.h include", strstr(hdr, "gc.h") == NULL);
            TESTEX("no obj field", strstr(hdr, "obj_t obj") == NULL);
            free(hdr);
        }

        char *impl = read_file("test_output/strings.c");
        TESTEX("strings.c created", impl != NULL);
        if (impl) {
            TESTEX("uses malloc not gc_alloc", strstr(impl, "gc_alloc") == NULL);
            TESTEX("no mark function", strstr(impl, "string_table_mark") == NULL);
            free(impl);
        }

        remove("test_output/strings.h");
        remove("test_output/strings.c");
        vmgen_spec_free(&spec);
    }

    /* --- GC integration: closures --- */
    MODULE("GC Integration - Closures");

    SUITE("GC-enabled closures have obj header + gc_alloc");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_closures(&spec, "test_output");
        TESTEX("gen_closures succeeds", rc == 0);

        char *hdr = read_file("test_output/closures.h");
        TESTEX("closures.h created", hdr != NULL);
        if (hdr) {
            TESTEX("closures.h includes gc.h", strstr(hdr, "#include \"gc.h\"") != NULL);
            TESTEX("upvalue_t has obj field", strstr(hdr, "obj_t obj") != NULL);
            TESTEX("closures.h has closure_mark", strstr(hdr, "closure_mark") != NULL);
            TESTEX("init_closure takes gc param", strstr(hdr, "gc_t *gc") != NULL);
            free(hdr);
        }

        char *impl = read_file("test_output/closures.c");
        TESTEX("closures.c created", impl != NULL);
        if (impl) {
            TESTEX("closures.c uses gc_alloc", strstr(impl, "gc_alloc") != NULL);
            TESTEX("closures.c uses OBJ_UPVALUE", strstr(impl, "OBJ_UPVALUE") != NULL);
            TESTEX("closures.c has closure_mark", strstr(impl, "closure_mark") != NULL);
            free(impl);
        }

        remove("test_output/closures.h");
        remove("test_output/closures.c");
        vmgen_spec_free(&spec);
    }

    SUITE("No-GC closures use plain malloc");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = false;
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_closures(&spec, "test_output");
        TESTEX("gen_closures succeeds", rc == 0);

        char *hdr = read_file("test_output/closures.h");
        TESTEX("closures.h created", hdr != NULL);
        if (hdr) {
            TESTEX("no gc.h include", strstr(hdr, "gc.h") == NULL);
            TESTEX("no obj field", strstr(hdr, "obj_t obj") == NULL);
            free(hdr);
        }

        char *impl = read_file("test_output/closures.c");
        TESTEX("closures.c created", impl != NULL);
        if (impl) {
            TESTEX("uses calloc not gc_alloc", strstr(impl, "gc_alloc") == NULL);
            TESTEX("no closure_mark", strstr(impl, "closure_mark") == NULL);
            free(impl);
        }

        remove("test_output/closures.h");
        remove("test_output/closures.c");
        vmgen_spec_free(&spec);
    }

    SUITE("GC obj enum has closure types");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_MARK_SWEEP;
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_gc(&spec, "test_output");
        TESTEX("gen_gc succeeds", rc == 0);

        char *hdr = read_file("test_output/gc.h");
        TESTEX("gc.h created", hdr != NULL);
        if (hdr) {
            TESTEX("gc.h has OBJ_CLOSURE", strstr(hdr, "OBJ_CLOSURE") != NULL);
            TESTEX("gc.h has OBJ_UPVALUE", strstr(hdr, "OBJ_UPVALUE") != NULL);
            free(hdr);
        }

        remove("test_output/gc.h");
        remove("test_output/gc.c");
        vmgen_spec_free(&spec);
    }

    /* --- Threaded dispatch --- */
    MODULE("Threaded Dispatch");

    SUITE("Auto dispatch has threaded + fnptr paths");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.dispatch = VMGEN_DISPATCH_AUTO;
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm auto succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("has __GNUC__ guard", strstr(content, "#if defined(__GNUC__)") != NULL);
            TESTEX("has dispatch_table labels", strstr(content, "&&op_") != NULL);
            TESTEX("has DISPATCH macro", strstr(content, "#define DISPATCH()") != NULL);
            TESTEX("has goto *", strstr(content, "goto *dispatch_table") != NULL);
            TESTEX("has #else fnptr fallback", strstr(content, "#else") != NULL);
            TESTEX("has fnptr handler table", strstr(content, "testvm_dispatch_table") != NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Threaded dispatch has computed gotos");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.dispatch = VMGEN_DISPATCH_THREADED;
        spec.instructions[0].action = vmgen_strdup("push constants[$index]");
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");
        spec.instructions[2].action = vmgen_strdup("halt");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm threaded succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("has op_PUSH_CONST label", strstr(content, "op_PUSH_CONST:") != NULL);
            TESTEX("has op_ADD label", strstr(content, "op_ADD:") != NULL);
            TESTEX("has op_HALT label", strstr(content, "op_HALT:") != NULL);
            TESTEX("has DISPATCH() calls", strstr(content, "DISPATCH();") != NULL);
            TESTEX("has #undef DISPATCH", strstr(content, "#undef DISPATCH") != NULL);
            TESTEX("has #error for non-GCC", strstr(content, "#error") != NULL);
            TESTEX("no fnptr table", strstr(content, "testvm_dispatch_table") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Switch dispatch has no computed gotos");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.dispatch = VMGEN_DISPATCH_SWITCH;
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm switch succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("has switch", strstr(content, "switch (opcode)") != NULL);
            TESTEX("no goto *", strstr(content, "goto *") == NULL);
            TESTEX("no DISPATCH macro", strstr(content, "#define DISPATCH()") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    /* --- Reference Counting GC --- */
    MODULE("Reference Counting GC");

    SUITE("Refcount gc.h has retain/release instead of mark/sweep");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_REFCOUNT;

        MKDIR("test_output");
        int rc = vmgen_gen_gc(&spec, "test_output");
        TESTEX("gen_gc succeeds", rc == 0);

        char *hdr = read_file("test_output/gc.h");
        TESTEX("gc.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has refcount field", strstr(hdr, "refcount") != NULL);
            TESTEX("has gc_retain", strstr(hdr, "gc_retain") != NULL);
            TESTEX("has gc_release", strstr(hdr, "gc_release") != NULL);
            TESTEX("no marked field", strstr(hdr, "int marked") == NULL);
            TESTEX("no gc_collect", strstr(hdr, "gc_collect") == NULL);
            TESTEX("no gc_inhibit", strstr(hdr, "gc_inhibit") == NULL);
            TESTEX("no mark_roots", strstr(hdr, "mark_roots") == NULL);
            free(hdr);
        }

        char *src = read_file("test_output/gc.c");
        TESTEX("gc.c created", src != NULL);
        if (src) {
            TESTEX("gc.c has gc_retain impl", strstr(src, "gc_retain") != NULL);
            TESTEX("gc.c has gc_release impl", strstr(src, "gc_release") != NULL);
            TESTEX("gc.c has refcount = 1", strstr(src, "refcount = 1") != NULL);
            TESTEX("gc.c has refcount--", strstr(src, "refcount--") != NULL);
            TESTEX("no gc_collect impl", strstr(src, "gc_collect") == NULL);
            free(src);
        }

        remove("test_output/gc.h");
        remove("test_output/gc.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Refcount gc.h has closure types when closures enabled");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_REFCOUNT;
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_gc(&spec, "test_output");
        TESTEX("gen_gc succeeds", rc == 0);

        char *hdr = read_file("test_output/gc.h");
        TESTEX("gc.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has OBJ_CLOSURE", strstr(hdr, "OBJ_CLOSURE") != NULL);
            TESTEX("has OBJ_UPVALUE", strstr(hdr, "OBJ_UPVALUE") != NULL);
            free(hdr);
        }

        remove("test_output/gc.h");
        remove("test_output/gc.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Refcount VM has no gc_inhibit/gc_set_roots");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.gc_enabled = true;
        spec.features.gc_strategy = VMGEN_GC_REFCOUNT;
        spec.dispatch = VMGEN_DISPATCH_SWITCH;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("no gc_inhibit", strstr(content, "gc_inhibit") == NULL);
            TESTEX("no gc_allow", strstr(content, "gc_allow") == NULL);
            TESTEX("no gc_set_roots", strstr(content, "gc_set_roots") == NULL);
            TESTEX("no mark_roots", strstr(content, "mark_roots") == NULL);
            free(content);
        }

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("still has gc.h include", strstr(hdr, "gc.h") != NULL);
            TESTEX("still has gc pointer", strstr(hdr, "gc_t *gc") != NULL);
            free(hdr);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    MODULE("Single Header Amalgamation");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        strcpy(spec.name, "testsh");
        strcpy(spec.version, "1.0");
        spec.stack.max_depth = 256;
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE | VMGEN_TYPE_BOOL;

        vmgen_instr_t *instr = &spec.instructions[spec.instruction_count++];
        strcpy(instr->name, "HALT");
        strcpy(instr->description, "Stop execution");
        instr->action = vmgen_strdup("halt");

        instr = &spec.instructions[spec.instruction_count++];
        strcpy(instr->name, "PUSH_CONST");
        strcpy(instr->description, "Push constant");
        instr->operands[instr->operand_count].type = VMGEN_OPERAND_UINT16;
        strcpy(instr->operands[instr->operand_count].name, "index");
        instr->operand_count = 1;
        instr->action = vmgen_strdup("push constants[$index]");

        MKDIR("test_output");

        SUITE("Generates single header file");
        {
            /* Generate all required files first */
            vmgen_gen_opcodes(&spec, "test_output");
            vmgen_gen_vm(&spec, "test_output");
            vmgen_gen_assembler(&spec, "test_output");
            vmgen_gen_disasm(&spec, "test_output");

            int rc = vmgen_gen_single_header(&spec, "test_output");
            TESTEX("gen_single_header succeeds", rc == 0);

            char *content = read_file("test_output/testsh.h");
            TESTEX("single header file created", content != NULL);
            if (content) {
                TESTEX("has guard ifndef", strstr(content, "#ifndef TESTSH_SINGLE_HEADER_H") != NULL);
                TESTEX("has IMPLEMENTATION ifdef", strstr(content, "#ifdef TESTSH_IMPLEMENTATION") != NULL);
                TESTEX("has vm struct", strstr(content, "testsh_vm_t") != NULL);
                TESTEX("has opcode enum", strstr(content, "TESTSH_OP_HALT") != NULL);
                TESTEX("has vm_init decl", strstr(content, "testsh_vm_init") != NULL);
                TESTEX("has vm_run impl", strstr(content, "testsh_vm_run") != NULL);
                TESTEX("no local includes", strstr(content, "#include \"vm.h\"") == NULL);
                TESTEX("no local include opcodes", strstr(content, "#include \"opcodes.h\"") == NULL);
                free(content);
            }
        }

        /* cleanup */
        remove("test_output/testsh.h");
        remove("test_output/opcodes.h");
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        remove("test_output/assembler.h");
        remove("test_output/assembler.c");
        remove("test_output/disasm.h");
        remove("test_output/disasm.c");
        vmgen_spec_free(&spec);
    }
}
