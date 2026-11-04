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
#include "codegen/gen_linemap.h"
#include "codegen/gen_verify.h"
#include "codegen/gen_optimizer.h"
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
        TESTEX("name table is a const array (no GCC unused-variable warning)",
               strstr(content, "static const char *const testvm_opcode_names[]") != NULL);
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

    /* --- vm_free generation --- */
    SUITE("Generate vm_free (fixed alloc)");
    ospec.dispatch = VMGEN_DISPATCH_SWITCH;
    rc = vmgen_gen_vm(&ospec, "test_output");
    TESTEX("gen_vm fixed succeeds", rc == 0);

    content = read_file("test_output/vm.h");
    TESTEX("vm.h created", content != NULL);
    if (content) {
        TESTEX("vm.h has vm_free proto", strstr(content, "testvm_vm_free") != NULL);
        free(content);
    }

    content = read_file("test_output/vm.c");
    TESTEX("vm.c created", content != NULL);
    if (content) {
        TESTEX("vm.c has vm_free impl", strstr(content, "void testvm_vm_free(") != NULL);
        free(content);
    }

    /* --- Dynamic allocation generation --- */
    MODULE("Dynamic Allocation Generation");

    SUITE("Generate vm with dynamic stack/pool/frames");
    {
        vmgen_spec_t dspec;
        make_test_spec(&dspec);
        dspec.stack.type = VMGEN_STACK_DYNAMIC;
        dspec.pool.type = VMGEN_ALLOC_DYNAMIC;
        dspec.pool.initial_capacity = 128;
        dspec.frame.type = VMGEN_ALLOC_DYNAMIC;
        dspec.frame.max_depth = 32;

        MKDIR("test_output");
        rc = vmgen_gen_vm(&dspec, "test_output");
        TESTEX("gen_vm dynamic succeeds", rc == 0);

        content = read_file("test_output/vm.h");
        TESTEX("vm.h created", content != NULL);
        if (content) {
            TESTEX("vm.h has stack pointer", strstr(content, "testvm_value_t *stack;") != NULL);
            TESTEX("vm.h has stack_capacity", strstr(content, "stack_capacity") != NULL);
            TESTEX("vm.h has pool pointer", strstr(content, "testvm_value_t *values;") != NULL);
            TESTEX("vm.h has pool capacity", strstr(content, "capacity") != NULL);
            TESTEX("vm.h has frame pointer", strstr(content, "testvm_frame_t *frames;") != NULL);
            TESTEX("vm.h has frame_capacity", strstr(content, "frame_capacity") != NULL);
            TESTEX("vm.h no MAX_CONSTANTS", strstr(content, "TESTVM_MAX_CONSTANTS") == NULL);
            TESTEX("vm.h no MAX_FRAMES", strstr(content, "TESTVM_MAX_FRAMES") == NULL);
            free(content);
        }

        content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has calloc for stack", strstr(content, "calloc") != NULL);
            TESTEX("vm.c has realloc for stack", strstr(content, "realloc") != NULL);
            TESTEX("vm.c has ensure_stack", strstr(content, "testvm_vm_ensure_stack") != NULL);
            TESTEX("vm.c has free in vm_free", strstr(content, "free(vm->stack)") != NULL);
            TESTEX("vm.c has free constants", strstr(content, "free(vm->constants.values)") != NULL);
            TESTEX("vm.c has free frames", strstr(content, "free(vm->frames)") != NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&dspec);
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
            TESTEX("closures.h includes vm.h", strstr(content, "#include \"vm.h\"") != NULL);
            TESTEX("closures.h has upvalue_t", strstr(content, "testvm_upvalue_t") != NULL);
            TESTEX("closures.h has closure_t", strstr(content, "testvm_closure_t") != NULL);
            TESTEX("closures.h has is_closed", strstr(content, "is_closed") != NULL);
            TESTEX("closures.h has init_closure", strstr(content, "testvm_init_closure") != NULL);
            TESTEX("closures.h has get_upvalue", strstr(content, "testvm_get_upvalue") != NULL);
            TESTEX("closures.h has set_upvalue", strstr(content, "testvm_set_upvalue") != NULL);
            TESTEX("closures.h has close_upvalue", strstr(content, "testvm_close_upvalue") != NULL);
            TESTEX("closures.h uses value_t for location", strstr(content, "testvm_value_t *location") != NULL);
            TESTEX("closures.h uses value_t for closed", strstr(content, "testvm_value_t closed") != NULL);
            TESTEX("closures.h get_upvalue returns value_t", strstr(content, "testvm_value_t testvm_get_upvalue") != NULL);
            TESTEX("closures.h set_upvalue takes value_t", strstr(content, "testvm_value_t value") != NULL);
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
            TESTEX("closures.c get_upvalue uses value_t", strstr(content, "testvm_value_t testvm_get_upvalue") != NULL);
            TESTEX("closures.c set_upvalue uses value_t", strstr(content, "testvm_value_t value") != NULL);
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

    SUITE("Parse exception handling actions");
    {
        vmgen_action_t action;
        int rc = vmgen_action_parse("try_begin $handler_offset", &action);
        TESTEX("parse try_begin succeeds", rc == 0);
        TESTEX("stmt is TRY_BEGIN", action.stmts[0].type == ACT_TRY_BEGIN);
        TESTEX("try_begin has expr", action.stmts[0].expr != NULL);
        vmgen_action_free(&action);

        rc = vmgen_action_parse("try_end", &action);
        TESTEX("parse try_end succeeds", rc == 0);
        TESTEX("stmt is TRY_END", action.stmts[0].type == ACT_TRY_END);
        vmgen_action_free(&action);

        rc = vmgen_action_parse("catch ex", &action);
        TESTEX("parse catch succeeds", rc == 0);
        TESTEX("stmt is CATCH", action.stmts[0].type == ACT_CATCH);
        TESTEX("catch ident is 'ex'", strcmp(action.stmts[0].ident, "ex") == 0);
        vmgen_action_free(&action);

        rc = vmgen_action_parse("pop ex; throw ex", &action);
        TESTEX("parse throw succeeds", rc == 0);
        TESTEX("has 2 statements", action.count == 2);
        TESTEX("stmt 1 is THROW", action.stmts[1].type == ACT_THROW);
        TESTEX("throw has expr", action.stmts[1].expr != NULL);
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

    SUITE("Emit modulo operator");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_INT64;
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        vmgen_action_t action;
        vmgen_action_parse("pop b; pop a; push a % b", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);

        TESTEX("emits single percent", strstr(buf.data, "a % b") != NULL);
        TESTEX("no double percent", strstr(buf.data, "%%") == NULL);

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

    SUITE("Tagged union: pop produces value_t");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        vmgen_action_t action;
        vmgen_action_parse("pop b; pop a; push a + b", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);

        TESTEX("pop produces value_t", strstr(buf.data, "test_value_t b = test_vm_pop(vm)") != NULL);
        TESTEX("push extracts .as.i64 for arithmetic", strstr(buf.data, "a.as.i64 + b.as.i64") != NULL);
        TESTEX("push wraps in INT64", strstr(buf.data, "TEST_INT64(") != NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("Tagged union: peek/push passthrough preserves type");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        vmgen_action_t action;
        vmgen_action_parse("peek a; push a", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);

        TESTEX("peek produces value_t", strstr(buf.data, "test_value_t a = vm->stack") != NULL);
        TESTEX("push passes through (no INT64)", strstr(buf.data, "test_vm_push(vm, a)") != NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("Tagged union: pop _ just discards");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        vmgen_action_t action;
        vmgen_action_parse("pop _", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);

        TESTEX("pop _ calls (void)vm_pop", strstr(buf.data, "(void)test_vm_pop(vm)") != NULL);
        TESTEX("no .as.i64 extraction", strstr(buf.data, ".as.i64") == NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("Tagged union: catch/throw preserve type");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.features.exception_handling = true;
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        /* catch ex; push ex */
        vmgen_action_t act1;
        vmgen_action_parse("catch ex; push ex", &act1);
        vmgen_buf_t buf1;
        vmgen_buf_init(&buf1);
        vmgen_action_emit(&buf1, "    ", &spec, &instr, &act1);

        TESTEX("catch produces value_t", strstr(buf1.data, "test_value_t ex = vm->current_exception") != NULL);
        TESTEX("push ex passthrough", strstr(buf1.data, "test_vm_push(vm, ex)") != NULL);

        vmgen_buf_free(&buf1);
        vmgen_action_free(&act1);

        /* pop ex; throw ex */
        vmgen_action_t act2;
        vmgen_action_parse("pop ex; throw ex", &act2);
        vmgen_buf_t buf2;
        vmgen_buf_init(&buf2);
        vmgen_action_emit(&buf2, "    ", &spec, &instr, &act2);

        TESTEX("throw assigns directly", strstr(buf2.data, "vm->current_exception = ex;") != NULL);

        vmgen_buf_free(&buf2);
        vmgen_action_free(&act2);
        vmgen_spec_free(&spec);
    }

    SUITE("Tagged union: jump_if_zero uses .as.i64");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "test");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));

        vmgen_action_t action;
        vmgen_action_parse("pop a; jump_if_zero a $offset", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit(&buf, "    ", &spec, &instr, &action);

        TESTEX("uses .as.i64 for comparison", strstr(buf.data, "a.as.i64 == 0") != NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

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
            TESTEX("vm.h has VAL_OBJECT enum", strstr(hdr, "VAL_OBJECT") != NULL);
            TESTEX("vm.h has obj pointer in union", strstr(hdr, "void   *obj") != NULL);
            TESTEX("vm.h has OBJ constructor", strstr(hdr, "_OBJ(v)") != NULL);
            free(hdr);
        }

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has mark_roots function", strstr(content, "mark_roots") != NULL);
            TESTEX("vm.c scans stack for OBJECT", strstr(content, "VAL_OBJECT") != NULL);
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

    /* --- Exception handling codegen --- */
    MODULE("Exception Handling Generation");

    SUITE("Exception-enabled VM has exception frames");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.features.exception_handling = true;

        /* Add TRY_BEGIN instruction */
        int idx = spec.instruction_count;
        snprintf(spec.instructions[idx].name, VMGEN_MAX_NAME, "TRY_BEGIN");
        spec.instructions[idx].operand_count = 1;
        snprintf(spec.instructions[idx].operands[0].name, VMGEN_MAX_NAME, "handler_offset");
        spec.instructions[idx].operands[0].type = VMGEN_OPERAND_INT16;
        spec.instructions[idx].action = vmgen_strdup("try_begin $handler_offset");
        spec.instruction_count++;

        /* Add THROW instruction */
        idx = spec.instruction_count;
        snprintf(spec.instructions[idx].name, VMGEN_MAX_NAME, "THROW");
        spec.instructions[idx].operand_count = 0;
        spec.instructions[idx].action = vmgen_strdup("pop ex; throw ex");
        spec.instruction_count++;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm with exceptions succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has exception_frame_t", strstr(hdr, "testvm_exception_frame_t") != NULL);
            TESTEX("vm.h has exception_frames array", strstr(hdr, "exception_frames") != NULL);
            TESTEX("vm.h has exception_count", strstr(hdr, "exception_count") != NULL);
            TESTEX("vm.h has current_exception", strstr(hdr, "current_exception") != NULL);
            TESTEX("vm.h has ERR_UNHANDLED_EXCEPTION", strstr(hdr, "ERR_UNHANDLED_EXCEPTION") != NULL);
            free(hdr);
        }

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c has exception frame push", strstr(content, "exception_frames[vm->exception_count]") != NULL);
            TESTEX("vm.c has handler_ip", strstr(content, "handler_ip") != NULL);
            TESTEX("vm.c has saved_sp", strstr(content, "saved_sp") != NULL);
            TESTEX("vm.c has unhandled error", strstr(content, "unhandled exception") != NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("No-exception VM has no exception frames");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.exception_handling = false;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm without exceptions succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has no exception_frame_t", strstr(hdr, "exception_frame_t") == NULL);
            TESTEX("vm.h has no exception_count", strstr(hdr, "exception_count") == NULL);
            free(hdr);
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

    /* --- closures.h auto-include in generated vm.c --- */
    MODULE("Closures Include in VM");

    SUITE("vm.c includes closures.h when closures enabled");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c includes closures.h",
                   strstr(content, "#include \"closures.h\"") != NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("vm.c does not include closures.h when closures disabled");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.closures = false;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *content = read_file("test_output/vm.c");
        TESTEX("vm.c created", content != NULL);
        if (content) {
            TESTEX("vm.c does not include closures.h",
                   strstr(content, "#include \"closures.h\"") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("vm.h has closure forward declarations when closures enabled");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *content = read_file("test_output/vm.h");
        TESTEX("vm.h created", content != NULL);
        if (content) {
            TESTEX("vm.h has upvalue forward decl",
                   strstr(content, "struct testvm_upvalue;") != NULL);
            TESTEX("vm.h has upvalue_t typedef",
                   strstr(content, "typedef struct testvm_upvalue testvm_upvalue_t;") != NULL);
            TESTEX("vm.h has closure forward decl",
                   strstr(content, "struct testvm_closure;") != NULL);
            TESTEX("vm.h has closure_t typedef",
                   strstr(content, "typedef struct testvm_closure testvm_closure_t;") != NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("vm.h has no closure forward declarations when closures disabled");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.closures = false;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *content = read_file("test_output/vm.h");
        TESTEX("vm.h created", content != NULL);
        if (content) {
            TESTEX("vm.h has no upvalue forward decl",
                   strstr(content, "struct testvm_upvalue;") == NULL);
            TESTEX("vm.h has no closure forward decl",
                   strstr(content, "struct testvm_closure;") == NULL);
            free(content);
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("closures.h has named closure struct tag");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.features.closures = true;

        MKDIR("test_output");
        int rc = vmgen_gen_closures(&spec, "test_output");
        TESTEX("gen_closures succeeds", rc == 0);

        char *content = read_file("test_output/closures.h");
        TESTEX("closures.h created", content != NULL);
        if (content) {
            TESTEX("closure struct has tag",
                   strstr(content, "struct testvm_closure {") != NULL);
            free(content);
        }

        remove("test_output/closures.h");
        remove("test_output/closures.c");
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
            /* Computed gotos are a GNU extension: -Wpedantic is silenced
             * around them (and restored afterwards) for warning-free output */
            const char *push = strstr(content, "#pragma GCC diagnostic push");
            const char *table = strstr(content, "static void *dispatch_table[]");
            const char *pop = strstr(content, "#pragma GCC diagnostic pop");
            TESTEX("pedantic silenced before label table", push && table && push < table);
            TESTEX("diagnostics restored after labels", pop && table && pop > table);
            TESTEX("clang label-as-value silenced",
                   strstr(content, "ignored \"-Wgnu-label-as-value\"") != NULL);
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

    MODULE("Error Recovery");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);

        MKDIR("test_output");
        vmgen_gen_vm(&spec, "test_output");

        SUITE("VM header has error types");
        {
            char *hdr = read_file("test_output/vm.h");
            TESTEX("vm.h created", hdr != NULL);
            if (hdr) {
                TESTEX("has error_t enum", strstr(hdr, "testvm_error_t") != NULL);
                TESTEX("has ERR_STACK_OVERFLOW", strstr(hdr, "TESTVM_ERR_STACK_OVERFLOW") != NULL);
                TESTEX("has ERR_STACK_UNDERFLOW", strstr(hdr, "TESTVM_ERR_STACK_UNDERFLOW") != NULL);
                TESTEX("has ERR_UNKNOWN_OPCODE", strstr(hdr, "TESTVM_ERR_UNKNOWN_OPCODE") != NULL);
                TESTEX("has ERR_DIV_BY_ZERO", strstr(hdr, "TESTVM_ERR_DIV_BY_ZERO") != NULL);
                TESTEX("has error_handler_t", strstr(hdr, "testvm_error_handler_t") != NULL);
                TESTEX("has last_error field", strstr(hdr, "last_error") != NULL);
                TESTEX("has error_handler field", strstr(hdr, "error_handler") != NULL);
                TESTEX("has set_error_handler", strstr(hdr, "testvm_vm_set_error_handler") != NULL);
                free(hdr);
            }
        }

        SUITE("VM source uses error handler");
        {
            char *src = read_file("test_output/vm.c");
            TESTEX("vm.c created", src != NULL);
            if (src) {
                TESTEX("has default handler", strstr(src, "testvm_default_error_handler") != NULL);
                TESTEX("has vm_error helper", strstr(src, "testvm_vm_error") != NULL);
                TESTEX("push uses vm_error", strstr(src, "testvm_vm_error(vm, TESTVM_ERR_STACK_OVERFLOW") != NULL);
                TESTEX("pop uses vm_error", strstr(src, "testvm_vm_error(vm, TESTVM_ERR_STACK_UNDERFLOW") != NULL);
                TESTEX("no direct fprintf overflow", strstr(src, "fprintf(stderr, \"stack overflow") == NULL);
                TESTEX("no direct fprintf underflow", strstr(src, "fprintf(stderr, \"stack underflow") == NULL);
                free(src);
            }
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    MODULE("Source Line Mapping");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);

        MKDIR("test_output");
        int rc = vmgen_gen_linemap(&spec, "test_output");
        TESTEX("gen_linemap succeeds", rc == 0);

        SUITE("linemap.h has types and API");
        {
            char *hdr = read_file("test_output/linemap.h");
            TESTEX("linemap.h created", hdr != NULL);
            if (hdr) {
                TESTEX("has entry type", strstr(hdr, "testvm_linemap_entry_t") != NULL);
                TESTEX("has map type", strstr(hdr, "testvm_linemap_t") != NULL);
                TESTEX("has init", strstr(hdr, "testvm_linemap_init") != NULL);
                TESTEX("has free", strstr(hdr, "testvm_linemap_free") != NULL);
                TESTEX("has add", strstr(hdr, "testvm_linemap_add") != NULL);
                TESTEX("has get_line", strstr(hdr, "testvm_linemap_get_line") != NULL);
                free(hdr);
            }
        }

        SUITE("linemap.c has implementation");
        {
            char *src = read_file("test_output/linemap.c");
            TESTEX("linemap.c created", src != NULL);
            if (src) {
                TESTEX("has init impl", strstr(src, "testvm_linemap_init") != NULL);
                TESTEX("has add impl", strstr(src, "testvm_linemap_add") != NULL);
                TESTEX("has binary search", strstr(src, "lo <= hi") != NULL);
                TESTEX("has realloc growth", strstr(src, "realloc") != NULL);
                free(src);
            }
        }

        remove("test_output/linemap.h");
        remove("test_output/linemap.c");
        vmgen_spec_free(&spec);
    }

    MODULE("Debug Stepping API");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);

        MKDIR("test_output");
        vmgen_gen_vm(&spec, "test_output");

        SUITE("VM header has debug stepping API");
        {
            char *hdr = read_file("test_output/vm.h");
            TESTEX("vm.h created", hdr != NULL);
            if (hdr) {
                TESTEX("has MAX_BREAKPOINTS", strstr(hdr, "TESTVM_MAX_BREAKPOINTS") != NULL);
                TESTEX("has vm_step", strstr(hdr, "testvm_vm_step") != NULL);
                TESTEX("has vm_set_breakpoint", strstr(hdr, "testvm_vm_set_breakpoint") != NULL);
                TESTEX("has vm_clear_breakpoint", strstr(hdr, "testvm_vm_clear_breakpoint") != NULL);
                TESTEX("has step_callback field", strstr(hdr, "step_callback") != NULL);
                TESTEX("has set_step_callback", strstr(hdr, "testvm_vm_set_step_callback") != NULL);
                free(hdr);
            }
        }

        SUITE("VM source has debug stepping impl");
        {
            char *src = read_file("test_output/vm.c");
            TESTEX("vm.c created", src != NULL);
            if (src) {
                TESTEX("has vm_step impl", strstr(src, "testvm_vm_step(testvm_vm_t") != NULL);
                TESTEX("has set_breakpoint impl", strstr(src, "testvm_vm_set_breakpoint(testvm_vm_t") != NULL);
                TESTEX("has clear_breakpoint impl", strstr(src, "testvm_vm_clear_breakpoint(testvm_vm_t") != NULL);
                TESTEX("step checks running", strstr(src, "vm->running") != NULL);
                TESTEX("step calls callback", strstr(src, "step_callback") != NULL);
                free(src);
            }
        }

        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    MODULE("Bytecode Verifier");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);

        MKDIR("test_output");
        int rc = vmgen_gen_verify(&spec, "test_output");
        TESTEX("gen_verify succeeds", rc == 0);

        SUITE("verify.h has types and API");
        {
            char *hdr = read_file("test_output/verify.h");
            TESTEX("verify.h created", hdr != NULL);
            if (hdr) {
                TESTEX("has error type", strstr(hdr, "testvm_verify_error_t") != NULL);
                TESTEX("has result type", strstr(hdr, "testvm_verify_result_t") != NULL);
                TESTEX("has init", strstr(hdr, "testvm_verify_init") != NULL);
                TESTEX("has free", strstr(hdr, "testvm_verify_free") != NULL);
                TESTEX("has verify func", strstr(hdr, "testvm_verify(") != NULL);
                TESTEX("takes const_count", strstr(hdr, "const_count") != NULL);
                free(hdr);
            }
        }

        SUITE("verify.c has validation logic");
        {
            char *src = read_file("test_output/verify.c");
            TESTEX("verify.c created", src != NULL);
            if (src) {
                TESTEX("checks opcode range", strstr(src, "OP_COUNT") != NULL);
                TESTEX("checks instruction fits", strstr(src, "extends past end") != NULL);
                TESTEX("checks PUSH_CONST index", strstr(src, "PUSH_CONST index out of range") != NULL);
                TESTEX("uses instruction_sizes", strstr(src, "instruction_sizes") != NULL);
                free(src);
            }
        }

        remove("test_output/verify.h");
        remove("test_output/verify.c");
        vmgen_spec_free(&spec);
    }

    {
        /* Test with jump instructions */
        vmgen_spec_t spec;
        make_test_spec(&spec);
        vmgen_instr_t *jmp = &spec.instructions[spec.instruction_count++];
        snprintf(jmp->name, VMGEN_MAX_NAME, "JUMP");
        jmp->operand_count = 1;
        snprintf(jmp->operands[0].name, VMGEN_MAX_NAME, "offset");
        jmp->operands[0].type = VMGEN_OPERAND_INT16;

        SUITE("Verifier with jump instructions");
        {
            MKDIR("test_output");
            int rc = vmgen_gen_verify(&spec, "test_output");
            TESTEX("gen_verify with jumps succeeds", rc == 0);

            char *src = read_file("test_output/verify.c");
            TESTEX("verify.c created", src != NULL);
            if (src) {
                TESTEX("checks jump target", strstr(src, "jump target out of range") != NULL);
                free(src);
            }
            remove("test_output/verify.h");
            remove("test_output/verify.c");
        }
        vmgen_spec_free(&spec);
    }

    /* ---- Typed action dispatch tests ---- */

    SUITE("typed_action: explicit type dispatch emission");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "tvm");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE;

        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));
        strcpy(instr.name, "ADD");
        instr.typed_action_count = 2;
        strcpy(instr.typed_action_types[0], "int64");
        instr.typed_actions[0] = vmgen_strdup("pop b; pop a; push a + b");
        strcpy(instr.typed_action_types[1], "double");
        instr.typed_actions[1] = vmgen_strdup("pop b; pop a; push a + b");
        instr.typed_action_default = NULL;

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit_typed(&buf, "    ", &spec, &instr);

        TESTEX("emits int64 branch", strstr(buf.data, "TVM_VAL_INT64") != NULL);
        TESTEX("emits double branch", strstr(buf.data, "TVM_VAL_DOUBLE") != NULL);
        TESTEX("uses .as.i64 in int64 path", strstr(buf.data, ".as.i64 + b.as.i64") != NULL);
        TESTEX("uses .as.f64 in double path", strstr(buf.data, ".as.f64 + b.as.f64") != NULL);
        TESTEX("wraps int64 in INT64", strstr(buf.data, "TVM_INT64(") != NULL);
        TESTEX("wraps double in DOUBLE", strstr(buf.data, "TVM_DOUBLE(") != NULL);
        TESTEX("pops as value_t before dispatch", strstr(buf.data, "tvm_value_t b = tvm_vm_pop(vm)") != NULL);
        TESTEX("type error fallback", strstr(buf.data, "type mismatch in ADD") != NULL);

        vmgen_buf_free(&buf);
        free(instr.typed_actions[0]);
        free(instr.typed_actions[1]);
        vmgen_spec_free(&spec);
    }

    SUITE("typed_action: with default clause");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "tvm");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE;

        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));
        strcpy(instr.name, "ADD");
        instr.typed_action_count = 1;
        strcpy(instr.typed_action_types[0], "int64");
        instr.typed_actions[0] = vmgen_strdup("pop b; pop a; push a + b");
        instr.typed_action_default = vmgen_strdup("return custom_add(vm, a, b);");

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        vmgen_action_emit_typed(&buf, "    ", &spec, &instr);

        TESTEX("emits int64 branch", strstr(buf.data, "TVM_VAL_INT64") != NULL);
        TESTEX("emits default clause", strstr(buf.data, "custom_add(vm, a, b)") != NULL);
        TESTEX("no type mismatch error", strstr(buf.data, "type mismatch") == NULL);

        vmgen_buf_free(&buf);
        free(instr.typed_actions[0]);
        free(instr.typed_action_default);
        vmgen_spec_free(&spec);
    }

    SUITE("auto-dispatch: BINOP with int64+double");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "avm");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE;

        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));
        strcpy(instr.name, "MUL");

        vmgen_action_t action;
        vmgen_action_parse("pop b; pop a; push a * b", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        int rc = vmgen_action_auto_dispatch(&buf, "    ", &spec, &instr, &action);

        TESTEX("auto-dispatch succeeds", rc == 0);
        TESTEX("emits int64 branch", strstr(buf.data, "AVM_VAL_INT64") != NULL);
        TESTEX("emits double branch", strstr(buf.data, "AVM_VAL_DOUBLE") != NULL);
        TESTEX("uses .as.i64 for int64 mul", strstr(buf.data, ".as.i64 * b.as.i64") != NULL);
        TESTEX("uses .as.f64 for double mul", strstr(buf.data, ".as.f64 * b.as.f64") != NULL);
        TESTEX("type error fallback", strstr(buf.data, "type mismatch in MUL") != NULL);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("auto-dispatch: skipped for single-type spec");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "svm");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64;  /* only int64 */

        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));
        strcpy(instr.name, "ADD");

        vmgen_action_t action;
        vmgen_action_parse("pop b; pop a; push a + b", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        int rc = vmgen_action_auto_dispatch(&buf, "    ", &spec, &instr, &action);

        TESTEX("auto-dispatch returns -1 for single type", rc == -1);
        TESTEX("no output emitted", buf.len == 0);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("auto-dispatch: skipped for non-arithmetic");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "nvm");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE;

        vmgen_instr_t instr;
        memset(&instr, 0, sizeof(instr));
        strcpy(instr.name, "DUP");

        vmgen_action_t action;
        vmgen_action_parse("peek a; push a", &action);

        vmgen_buf_t buf;
        vmgen_buf_init(&buf);
        int rc = vmgen_action_auto_dispatch(&buf, "    ", &spec, &instr, &action);

        TESTEX("auto-dispatch returns -1 for non-arithmetic", rc == -1);

        vmgen_buf_free(&buf);
        vmgen_action_free(&action);
        vmgen_spec_free(&spec);
    }

    SUITE("typed_action: wired into gen_vm (fnptr dispatch)");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        snprintf(spec.name, sizeof(spec.name), "tdvm");
        snprintf(spec.version, sizeof(spec.version), "1.0");
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags = VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE;
        spec.dispatch = VMGEN_DISPATCH_FNPTR;

        vmgen_instr_t *halt = &spec.instructions[spec.instruction_count++];
        strcpy(halt->name, "HALT");
        halt->action = vmgen_strdup("halt");

        vmgen_instr_t *add = &spec.instructions[spec.instruction_count++];
        strcpy(add->name, "ADD");
        add->typed_action_count = 2;
        strcpy(add->typed_action_types[0], "int64");
        add->typed_actions[0] = vmgen_strdup("pop b; pop a; push a + b");
        strcpy(add->typed_action_types[1], "double");
        add->typed_actions[1] = vmgen_strdup("pop b; pop a; push a + b");

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm with typed_action succeeds", rc == 0);

        char *src = read_file("test_output/vm.c");
        TESTEX("vm.c created", src != NULL);
        if (src) {
            TESTEX("has int64 type guard", strstr(src, "TDVM_VAL_INT64") != NULL);
            TESTEX("has double type guard", strstr(src, "TDVM_VAL_DOUBLE") != NULL);
            TESTEX("uses i64 accessor", strstr(src, ".as.i64 + b.as.i64") != NULL);
            TESTEX("uses f64 accessor", strstr(src, ".as.f64 + b.as.f64") != NULL);
            free(src);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }
}

void test_optimizer(void) {
    MODULE("Bytecode Optimizer");
    {
        /* Need a spec with actions for optimizer to detect arithmetic/terminal patterns */
        vmgen_spec_t spec;
        make_test_spec(&spec);

        /* Add action strings (action is a heap-allocated char*, not a fixed buffer) */
        spec.instructions[0].action = vmgen_strdup("push constants[$index]");
        spec.instructions[1].action = vmgen_strdup("pop b; pop a; push a + b");
        spec.instructions[2].action = vmgen_strdup("halt");

        /* Add POP instruction */
        vmgen_instr_t *pop_instr = &spec.instructions[spec.instruction_count++];
        snprintf(pop_instr->name, VMGEN_MAX_NAME, "POP");
        pop_instr->operand_count = 0;
        pop_instr->action = vmgen_strdup("pop a");

        /* Add RETURN instruction */
        vmgen_instr_t *ret = &spec.instructions[spec.instruction_count++];
        snprintf(ret->name, VMGEN_MAX_NAME, "RETURN");
        ret->operand_count = 0;
        ret->action = vmgen_strdup("return");

        /* Add SUB instruction */
        vmgen_instr_t *sub = &spec.instructions[spec.instruction_count++];
        snprintf(sub->name, VMGEN_MAX_NAME, "SUB");
        sub->operand_count = 0;
        sub->action = vmgen_strdup("pop b; pop a; push a - b");

        /* Add JUMP instruction */
        vmgen_instr_t *jmp = &spec.instructions[spec.instruction_count++];
        snprintf(jmp->name, VMGEN_MAX_NAME, "JUMP");
        jmp->operand_count = 1;
        snprintf(jmp->operands[0].name, VMGEN_MAX_NAME, "offset");
        jmp->operands[0].type = VMGEN_OPERAND_INT16;

        MKDIR("test_output");
        int rc = vmgen_gen_optimizer(&spec, "test_output");
        TESTEX("gen_optimizer succeeds", rc == 0);

        SUITE("optimizer.h has types and API");
        {
            char *hdr = read_file("test_output/optimizer.h");
            TESTEX("optimizer.h created", hdr != NULL);
            if (hdr) {
                TESTEX("has OP_NOP define", strstr(hdr, "OP_NOP") != NULL);
                TESTEX("has opt_stats_t", strstr(hdr, "testvm_opt_stats_t") != NULL);
                TESTEX("has optimize func", strstr(hdr, "testvm_optimize(") != NULL);
                TESTEX("has strip_nops func", strstr(hdr, "testvm_optimize_strip_nops(") != NULL);
                TESTEX("has constants_folded field", strstr(hdr, "constants_folded") != NULL);
                TESTEX("has dead_code_bytes field", strstr(hdr, "dead_code_bytes") != NULL);
                TESTEX("has peephole_opts field", strstr(hdr, "peephole_opts") != NULL);
                TESTEX("includes opcodes.h", strstr(hdr, "#include \"opcodes.h\"") != NULL);
                TESTEX("includes vm.h", strstr(hdr, "#include \"vm.h\"") != NULL);
                free(hdr);
            }
        }

        SUITE("optimizer.c has optimization passes");
        {
            char *src = read_file("test_output/optimizer.c");
            TESTEX("optimizer.c created", src != NULL);
            if (src) {
                /* Constant folding */
                TESTEX("has fold_constants", strstr(src, "fold_constants") != NULL);
                TESTEX("checks PUSH_CONST for folding", strstr(src, "OP_PUSH_CONST") != NULL);
                TESTEX("checks ADD for folding", strstr(src, "OP_ADD") != NULL);
                TESTEX("checks SUB for folding", strstr(src, "OP_SUB") != NULL);

                /* Dead code elimination */
                TESTEX("has eliminate_dead_code", strstr(src, "eliminate_dead_code") != NULL);
                TESTEX("checks HALT terminal", strstr(src, "OP_HALT") != NULL);
                TESTEX("checks RETURN terminal", strstr(src, "OP_RETURN") != NULL);

                /* Peephole */
                TESTEX("has peephole", strstr(src, "peephole") != NULL);
                TESTEX("checks POP for peephole", strstr(src, "OP_POP") != NULL);

                /* NOP fill */
                TESTEX("has nop_fill", strstr(src, "nop_fill") != NULL);
                TESTEX("uses OP_NOP", strstr(src, "OP_NOP") != NULL);

                /* NOP strip */
                TESTEX("has offset map", strstr(src, "offmap") != NULL);
                TESTEX("patches JUMP offsets", strstr(src, "OP_JUMP") != NULL);

                /* Includes stdlib for malloc */
                TESTEX("includes stdlib.h", strstr(src, "#include <stdlib.h>") != NULL);

                free(src);
            }
        }

        remove("test_output/optimizer.h");
        remove("test_output/optimizer.c");
        vmgen_spec_free(&spec);
    }

    /* --- Value Type Completeness --- */
    MODULE("Value Type Generation");

    SUITE("NIL type always generated");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has VAL_NIL", strstr(hdr, "VAL_NIL") != NULL);
            TESTEX("vm.h has NIL_VAL constructor", strstr(hdr, "NIL_VAL") != NULL);
            TESTEX("vm.h has VAL_INT64", strstr(hdr, "VAL_INT64") != NULL);
            TESTEX("vm.h has VAL_DOUBLE", strstr(hdr, "VAL_DOUBLE") != NULL);
            TESTEX("vm.h has VAL_BOOL", strstr(hdr, "VAL_BOOL") != NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("OBJECT type from type_flags without GC");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags |= VMGEN_TYPE_OBJECT;
        spec.features.gc_enabled = false;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has VAL_OBJECT", strstr(hdr, "VAL_OBJECT") != NULL);
            TESTEX("vm.h has void *obj", strstr(hdr, "void   *obj") != NULL);
            TESTEX("vm.h has OBJ constructor", strstr(hdr, "_OBJ(v)") != NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("STRING type from type_flags");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        spec.type_flags |= VMGEN_TYPE_STRING;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("vm.h has VAL_STRING", strstr(hdr, "VAL_STRING") != NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    /* --- VM Extension Fields --- */
    MODULE("VM Extension Fields");

    SUITE("Extension fields appear in VM struct");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        strcpy(spec.extensions[0].name, "globals");
        strcpy(spec.extensions[0].type_str, "void *");
        strcpy(spec.extensions[0].comment, "Global variable table");
        strcpy(spec.extensions[1].name, "natives");
        strcpy(spec.extensions[1].type_str, "native_table_t");
        strcpy(spec.extensions[1].include, "native.h");
        spec.extension_count = 2;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has globals field", strstr(hdr, "void * globals") != NULL);
            TESTEX("has globals comment", strstr(hdr, "Global variable table") != NULL);
            TESTEX("has natives field", strstr(hdr, "native_table_t natives") != NULL);
            TESTEX("includes native.h", strstr(hdr, "#include \"native.h\"") != NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Function pointer extension fields");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        strcpy(spec.extensions[0].name, "arith_fallback");
        strcpy(spec.extensions[0].type_str, "void (*)(testvm_vm_t *, testvm_value_t, testvm_value_t, int)");
        spec.extension_count = 1;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has correct fnptr syntax", strstr(hdr, "void (*arith_fallback)(testvm_vm_t *, testvm_value_t, testvm_value_t, int);") != NULL);
            TESTEX("no invalid TYPE NAME pattern", strstr(hdr, "int) arith_fallback") == NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Function pointer extension with comment");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        strcpy(spec.extensions[0].name, "on_error");
        strcpy(spec.extensions[0].type_str, "int (*)(int, const char *)");
        strcpy(spec.extensions[0].comment, "Error callback");
        spec.extension_count = 1;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has correct fnptr with comment", strstr(hdr, "int (*on_error)(int, const char *); /* Error callback */") != NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    /* --- Cleanup Hooks --- */
    MODULE("Cleanup Hooks");

    SUITE("vm_free calls cleanup callback");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has cleanup field", strstr(hdr, "void (*cleanup)") != NULL);
            TESTEX("has set_cleanup prototype", strstr(hdr, "vm_set_cleanup") != NULL);
            free(hdr);
        }

        char *src = read_file("test_output/vm.c");
        TESTEX("vm.c created", src != NULL);
        if (src) {
            TESTEX("vm_free calls cleanup", strstr(src, "vm->cleanup") != NULL);
            TESTEX("has set_cleanup impl", strstr(src, "vm_set_cleanup") != NULL);
            free(src);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    /* --- Exposed Dispatch Table --- */
    MODULE("Exposed Dispatch Table");

    SUITE("Dispatch table non-static when exposed");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        spec.dispatch = VMGEN_DISPATCH_FNPTR;
        spec.features.expose_dispatch_table = true;

        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("has extern dispatch table", strstr(hdr, "extern") != NULL);
            TESTEX("has handler_t typedef", strstr(hdr, "handler_t") != NULL);
            free(hdr);
        }

        char *src = read_file("test_output/vm.c");
        TESTEX("vm.c created", src != NULL);
        if (src) {
            TESTEX("handlers not static", strstr(src, "static int handle_") == NULL);
            free(src);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    /* --- User-defined value types --- */
    SUITE("Custom types: vm.h enum, union and constructors");
    {
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        int rc = vmgen_parse_spec("specs/custom_types.yaml", &spec);
        TESTEX("parse custom_types.yaml", rc == 0);
        MKDIR("test_output");
        rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);

        char *hdr = read_file("test_output/vm.h");
        TESTEX("vm.h created", hdr != NULL);
        if (hdr) {
            TESTEX("enum has CELL", strstr(hdr, "CTVM_VAL_CELL,") != NULL);
            TESTEX("enum has HANDLE", strstr(hdr, "CTVM_VAL_HANDLE,") != NULL);
            TESTEX("enum comment emitted",
                   strstr(hdr, "/* boxed variable for by-reference capture */") != NULL);
            TESTEX("custom types follow OBJECT",
                   strstr(hdr, "CTVM_VAL_OBJECT,") < strstr(hdr, "CTVM_VAL_CELL,"));
            TESTEX("pointer constructor uses .as.obj",
                   strstr(hdr, "#define CTVM_CELL(v)  ((ctvm_value_t){ .type = CTVM_VAL_CELL, "
                               ".as.obj = (void*)(v) })") != NULL);
            TESTEX("int64 constructor uses .as.i64",
                   strstr(hdr, "#define CTVM_HANDLE(v)  ((ctvm_value_t){ .type = CTVM_VAL_HANDLE, "
                               ".as.i64 = (v) })") != NULL);
            TESTEX("double constructor uses .as.f64",
                   strstr(hdr, "#define CTVM_WEIGHT(v)  ((ctvm_value_t){ .type = CTVM_VAL_WEIGHT, "
                               ".as.f64 = (v) })") != NULL);
            free(hdr);
        }

        char *src = read_file("test_output/vm.c");
        TESTEX("vm.c created", src != NULL);
        if (src) {
            const char *ref = strstr(src, "static int ctvm_is_gc_ref(ctvm_value_t v)");
            TESTEX("gc ref helper emitted", ref != NULL);
            if (ref) {
                const char *end = strstr(ref, "\n}\n");
                const char *gcbox = strstr(ref, "case CTVM_VAL_GCBOX:");
                const char *cell = strstr(ref, "case CTVM_VAL_CELL:");
                TESTEX("gc:true type is a GC root", gcbox && end && gcbox < end);
                TESTEX("gc:false type is not a GC root", !cell || (end && cell > end));
            }
            TESTEX("mark_roots uses helper", strstr(src, "if (ctvm_is_gc_ref(vm->stack[i]))") != NULL);
            free(src);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");

        rc = vmgen_gen_bytecode(&spec, "test_output");
        TESTEX("gen_bytecode succeeds", rc == 0);
        char *bc = read_file("test_output/bytecode.c");
        if (bc) {
            TESTEX("bytecode save rejects custom types",
                   strstr(bc, "case CTVM_VAL_CELL:") != NULL &&
                   strstr(bc, "cannot be serialized") != NULL);
            free(bc);
        }
        remove("test_output/bytecode.h");
        remove("test_output/bytecode.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Custom pointer type without 'object' still gets .as.obj");
    {
        vmgen_spec_t spec;
        make_test_spec(&spec);
        snprintf(spec.custom_types[0].name, VMGEN_MAX_NAME, "cell");
        spec.custom_types[0].payload = VMGEN_PAYLOAD_POINTER;
        spec.custom_type_count = 1;
        MKDIR("test_output");
        int rc = vmgen_gen_vm(&spec, "test_output");
        TESTEX("gen_vm succeeds", rc == 0);
        char *hdr = read_file("test_output/vm.h");
        if (hdr) {
            TESTEX("no OBJECT enum", strstr(hdr, "TESTVM_VAL_OBJECT") == NULL);
            TESTEX("union has obj field", strstr(hdr, "void   *obj;") != NULL);
            TESTEX("CELL constructor", strstr(hdr, "#define TESTVM_CELL(v)") != NULL);
            free(hdr);
        }
        remove("test_output/vm.h");
        remove("test_output/vm.c");
        vmgen_spec_free(&spec);
    }

    SUITE("Bad custom type payload is a parse error");
    {
        FILE *f = fopen("test_output/bad_payload.yaml", "w");
        if (f) {
            fputs("vm:\n  name: \"bad\"\n  version: \"1.0\"\n"
                  "  stack: { type: fixed, max_depth: 16, value_type: tagged_union }\n"
                  "  types: [int64, { name: cell, payload: float }]\n"
                  "  instructions:\n    - { name: HALT, operands: [], action: \"halt\" }\n", f);
            fclose(f);
        }
        vmgen_spec_t spec;
        vmgen_spec_init(&spec);
        TESTEX("unknown payload rejected",
               vmgen_parse_spec("test_output/bad_payload.yaml", &spec) != 0);
        vmgen_spec_free(&spec);
        remove("test_output/bad_payload.yaml");
    }
}
