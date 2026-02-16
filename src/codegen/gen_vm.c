#include "gen_vm.h"
#include "emit.h"
#include "../util/buffer.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void make_upper(char *dst, const char *src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

static const char *c_type_for_operand(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  return "uint8_t";
    case VMGEN_OPERAND_UINT16: return "uint16_t";
    case VMGEN_OPERAND_UINT32: return "uint32_t";
    case VMGEN_OPERAND_INT8:   return "int8_t";
    case VMGEN_OPERAND_INT16:  return "int16_t";
    case VMGEN_OPERAND_INT32:  return "int32_t";
    }
    return "uint8_t";
}

static int operand_size(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  case VMGEN_OPERAND_INT8:  return 1;
    case VMGEN_OPERAND_UINT16: case VMGEN_OPERAND_INT16: return 2;
    case VMGEN_OPERAND_UINT32: case VMGEN_OPERAND_INT32: return 4;
    }
    return 1;
}

/* Emit read macros for decoding operands from the bytecode stream */
static void emit_read_macros(vmgen_buf_t *buf) {
    vmgen_buf_append(buf,
        "#define READ_UINT8()  (vm->code[vm->ip++])\n"
        "#define READ_INT8()   ((int8_t)vm->code[vm->ip++])\n"
        "#define READ_UINT16() (vm->ip += 2, \\\n"
        "    (uint16_t)(vm->code[vm->ip-2] | (vm->code[vm->ip-1] << 8)))\n"
        "#define READ_INT16()  ((int16_t)READ_UINT16())\n"
        "#define READ_UINT32() (vm->ip += 4, \\\n"
        "    (uint32_t)(vm->code[vm->ip-4] | (vm->code[vm->ip-3] << 8) | \\\n"
        "              (vm->code[vm->ip-2] << 16) | (vm->code[vm->ip-1] << 24)))\n"
        "#define READ_INT32()  ((int32_t)READ_UINT32())\n\n"
    );
}

static const char *read_macro_for(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  return "READ_UINT8()";
    case VMGEN_OPERAND_UINT16: return "READ_UINT16()";
    case VMGEN_OPERAND_UINT32: return "READ_UINT32()";
    case VMGEN_OPERAND_INT8:   return "READ_INT8()";
    case VMGEN_OPERAND_INT16:  return "READ_INT16()";
    case VMGEN_OPERAND_INT32:  return "READ_INT32()";
    }
    return "READ_UINT8()";
}

/* ---- Generate vm.h ---- */

static int gen_vm_header(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_VM_H", upper);

    char path[512];
    snprintf(path, sizeof(path), "%s/vm.h", output_dir);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "vm.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf, "#include <stdint.h>\n");
    vmgen_buf_append(&buf, "#include <stddef.h>\n");
    vmgen_buf_append(&buf, "#include \"opcodes.h\"\n\n");

    /* Value type */
    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        vmgen_buf_appendf(&buf,
            "typedef enum {\n"
            "    %s_VAL_INT64,\n"
            "    %s_VAL_DOUBLE,\n"
            "    %s_VAL_BOOL,\n"
            "} %s_val_type_t;\n\n",
            upper, upper, upper, spec->name);

        vmgen_buf_appendf(&buf,
            "typedef struct {\n"
            "    %s_val_type_t type;\n"
            "    union {\n"
            "        int64_t i64;\n"
            "        double  f64;\n"
            "        int     boolean;\n"
            "    } as;\n"
            "} %s_value_t;\n\n",
            spec->name, spec->name);

        /* Value constructors */
        vmgen_buf_appendf(&buf,
            "#define %s_INT64(v)  ((%s_value_t){ .type = %s_VAL_INT64,  .as.i64 = (v) })\n"
            "#define %s_DOUBLE(v) ((%s_value_t){ .type = %s_VAL_DOUBLE, .as.f64 = (v) })\n"
            "#define %s_BOOL(v)   ((%s_value_t){ .type = %s_VAL_BOOL,   .as.boolean = (v) })\n\n",
            upper, spec->name, upper,
            upper, spec->name, upper,
            upper, spec->name, upper);
    } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
        vmgen_buf_appendf(&buf, "typedef double %s_value_t;\n\n", spec->name);
    } else {
        vmgen_buf_appendf(&buf, "typedef int64_t %s_value_t;\n\n", spec->name);
    }

    /* VM state struct */
    vmgen_buf_appendf(&buf,
        "typedef struct {\n"
        "    const uint8_t *code;\n"
        "    size_t code_len;\n"
        "    size_t ip;\n"
        "    %s_value_t stack[%d];\n"
        "    int sp;\n"
        "    int running;\n"
        "} %s_vm_t;\n\n",
        spec->name, spec->stack.max_depth, spec->name);

    /* API prototypes */
    vmgen_buf_appendf(&buf,
        "void %s_vm_init(%s_vm_t *vm);\n"
        "int  %s_vm_run(%s_vm_t *vm, const uint8_t *code, size_t code_len);\n"
        "void %s_vm_push(%s_vm_t *vm, %s_value_t value);\n"
        "%s_value_t %s_vm_pop(%s_vm_t *vm);\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}

/* ---- Generate vm.c (switch dispatch) ---- */

static void emit_switch_dispatch(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                                 const char *upper) {
    vmgen_buf_append(buf,
        "    while (vm->running && vm->ip < vm->code_len) {\n"
        "        uint8_t opcode = vm->code[vm->ip++];\n"
        "        switch (opcode) {\n");

    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(buf, "        case %s_OP_%s: {\n",
                          upper, instr->name);

        /* Decode operands */
        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(buf, "            %s %s = %s;\n",
                              c_type_for_operand(instr->operands[j].type),
                              instr->operands[j].name,
                              read_macro_for(instr->operands[j].type));
        }

        /* Suppress unused variable warnings for operands */
        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(buf, "            (void)%s;\n",
                              instr->operands[j].name);
        }

        vmgen_buf_appendf(buf, "            /* TODO: implement %s */\n",
                          instr->name);
        vmgen_buf_append(buf, "            break;\n        }\n");
    }

    vmgen_buf_appendf(buf,
        "        default:\n"
        "            fprintf(stderr, \"unknown opcode %%d at ip %%zu\\n\",\n"
        "                    opcode, vm->ip - 1);\n"
        "            return -1;\n"
        "        }\n"
        "    }\n");
}

static void emit_fnptr_dispatch(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                                const char *upper) {
    /* Forward-declare handler type */
    vmgen_buf_appendf(buf,
        "typedef int (*%s_handler_t)(%s_vm_t *vm);\n\n",
        spec->name, spec->name);

    /* Generate handler functions */
    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(buf,
            "static int handle_%s(%s_vm_t *vm) {\n",
            instr->name, spec->name);

        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(buf, "    %s %s = %s;\n",
                              c_type_for_operand(instr->operands[j].type),
                              instr->operands[j].name,
                              read_macro_for(instr->operands[j].type));
        }
        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(buf, "    (void)%s;\n",
                              instr->operands[j].name);
        }

        vmgen_buf_appendf(buf,
            "    /* TODO: implement %s */\n"
            "    return 0;\n"
            "}\n\n",
            instr->name);
    }

    /* Dispatch table */
    vmgen_buf_appendf(buf,
        "static %s_handler_t %s_dispatch_table[] = {\n",
        spec->name, spec->name);
    for (int i = 0; i < spec->instruction_count; i++) {
        vmgen_buf_appendf(buf, "    handle_%s,\n",
                          spec->instructions[i].name);
    }
    vmgen_buf_append(buf, "};\n\n");

    (void)upper;
}

static void emit_fnptr_loop(vmgen_buf_t *buf, const vmgen_spec_t *spec) {
    vmgen_buf_appendf(buf,
        "    while (vm->running && vm->ip < vm->code_len) {\n"
        "        uint8_t opcode = vm->code[vm->ip++];\n"
        "        if (opcode >= %s_OP_COUNT) {\n"
        "            fprintf(stderr, \"unknown opcode %%d at ip %%zu\\n\",\n"
        "                    opcode, vm->ip - 1);\n"
        "            return -1;\n"
        "        }\n"
        "        int rc = %s_dispatch_table[opcode](vm);\n"
        "        if (rc != 0) return rc;\n"
        "    }\n",
        spec->name, spec->name);
}

static int gen_vm_source(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    char path[512];
    snprintf(path, sizeof(path), "%s/vm.c", output_dir);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "vm.c", spec);

    vmgen_buf_append(&buf,
        "#include \"vm.h\"\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n\n");

    emit_read_macros(&buf);

    /* Stack operations */
    vmgen_buf_appendf(&buf,
        "void %s_vm_push(%s_vm_t *vm, %s_value_t value) {\n"
        "    if (vm->sp >= %d) {\n"
        "        fprintf(stderr, \"stack overflow\\n\");\n"
        "        vm->running = 0;\n"
        "        return;\n"
        "    }\n"
        "    vm->stack[vm->sp++] = value;\n"
        "}\n\n",
        spec->name, spec->name, spec->name, spec->stack.max_depth);

    vmgen_buf_appendf(&buf,
        "%s_value_t %s_vm_pop(%s_vm_t *vm) {\n"
        "    if (vm->sp <= 0) {\n"
        "        fprintf(stderr, \"stack underflow\\n\");\n"
        "        vm->running = 0;\n",
        spec->name, spec->name, spec->name);

    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        vmgen_buf_appendf(&buf,
            "        return %s_INT64(0);\n", upper);
    } else {
        vmgen_buf_append(&buf, "        return 0;\n");
    }

    vmgen_buf_append(&buf,
        "    }\n"
        "    return vm->stack[--vm->sp];\n"
        "}\n\n");

    /* vm_init */
    vmgen_buf_appendf(&buf,
        "void %s_vm_init(%s_vm_t *vm) {\n"
        "    memset(vm, 0, sizeof(*vm));\n"
        "}\n\n",
        spec->name, spec->name);

    /* Emit function-pointer handlers if fnptr or auto dispatch */
    if (spec->dispatch == VMGEN_DISPATCH_FNPTR ||
        spec->dispatch == VMGEN_DISPATCH_AUTO) {
        emit_fnptr_dispatch(&buf, spec, upper);
    }

    /* vm_run */
    vmgen_buf_appendf(&buf,
        "int %s_vm_run(%s_vm_t *vm, const uint8_t *code, size_t code_len) {\n"
        "    vm->code = code;\n"
        "    vm->code_len = code_len;\n"
        "    vm->ip = 0;\n"
        "    vm->sp = 0;\n"
        "    vm->running = 1;\n\n",
        spec->name, spec->name);

    switch (spec->dispatch) {
    case VMGEN_DISPATCH_SWITCH:
        emit_switch_dispatch(&buf, spec, upper);
        break;
    case VMGEN_DISPATCH_FNPTR:
        emit_fnptr_loop(&buf, spec);
        break;
    case VMGEN_DISPATCH_AUTO:
        vmgen_buf_append(&buf, "#if defined(__GNUC__) || defined(__clang__)\n");
        vmgen_buf_append(&buf, "    /* TODO: threaded dispatch */\n");
        vmgen_buf_append(&buf, "    /* Falling through to fnptr for now */\n");
        vmgen_buf_append(&buf, "#endif\n");
        emit_fnptr_loop(&buf, spec);
        break;
    case VMGEN_DISPATCH_THREADED:
        vmgen_buf_append(&buf, "#if defined(__GNUC__) || defined(__clang__)\n");
        vmgen_buf_append(&buf, "    /* TODO: threaded dispatch with computed gotos */\n");
        vmgen_buf_append(&buf, "#else\n");
        vmgen_buf_append(&buf, "#error \"Threaded dispatch requires GCC or Clang\"\n");
        vmgen_buf_append(&buf, "#endif\n");
        break;
    }

    vmgen_buf_append(&buf, "    return 0;\n}\n");

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}

int vmgen_gen_vm(const vmgen_spec_t *spec, const char *output_dir) {
    int rc = gen_vm_header(spec, output_dir);
    if (rc != 0) return rc;
    return gen_vm_source(spec, output_dir);
}
