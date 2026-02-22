#include "gen_vm.h"
#include "emit.h"
#include "action_parser.h"
#include "action_emit.h"
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
    vmgen_buf_append(&buf, "#include \"opcodes.h\"\n");
    if (spec->features.gc_enabled) {
        vmgen_buf_append(&buf, "#include \"gc.h\"\n");
    }
    for (int i = 0; i < spec->extension_count; i++) {
        if (spec->extensions[i].include[0]) {
            vmgen_buf_appendf(&buf, "#include \"%s\"\n", spec->extensions[i].include);
        }
    }
    vmgen_buf_appendc(&buf, '\n');

    /* Value type */
    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        int has_string = (spec->type_flags & VMGEN_TYPE_STRING) != 0;
        /* GC implies object type support */
        int has_object = (spec->type_flags & VMGEN_TYPE_OBJECT) != 0 ||
                         spec->features.gc_enabled;

        /* Enum: always NIL + INT64 + DOUBLE + BOOL, then extras from types list */
        vmgen_buf_appendf(&buf,
            "typedef enum {\n"
            "    %s_VAL_NIL,\n"
            "    %s_VAL_INT64,\n"
            "    %s_VAL_DOUBLE,\n"
            "    %s_VAL_BOOL,\n",
            upper, upper, upper, upper);
        if (has_string) {
            vmgen_buf_appendf(&buf,
                "    %s_VAL_STRING,\n", upper);
        }
        if (has_object) {
            vmgen_buf_appendf(&buf,
                "    %s_VAL_OBJECT,\n", upper);
        }
        vmgen_buf_appendf(&buf,
            "} %s_val_type_t;\n\n", spec->name);

        /* Tagged union struct */
        vmgen_buf_appendf(&buf,
            "typedef struct {\n"
            "    %s_val_type_t type;\n"
            "    union {\n"
            "        int64_t i64;\n"
            "        double  f64;\n"
            "        int     boolean;\n",
            spec->name);
        if (has_object) {
            vmgen_buf_append(&buf, "        void   *obj;\n");
        }
        vmgen_buf_appendf(&buf,
            "    } as;\n"
            "} %s_value_t;\n\n", spec->name);

        /* Value constructors */
        vmgen_buf_appendf(&buf,
            "#define %s_NIL_VAL   ((%s_value_t){ .type = %s_VAL_NIL,    .as.i64 = 0 })\n"
            "#define %s_INT64(v)  ((%s_value_t){ .type = %s_VAL_INT64,  .as.i64 = (v) })\n"
            "#define %s_DOUBLE(v) ((%s_value_t){ .type = %s_VAL_DOUBLE, .as.f64 = (v) })\n"
            "#define %s_BOOL(v)   ((%s_value_t){ .type = %s_VAL_BOOL,   .as.boolean = (v) })\n",
            upper, spec->name, upper,
            upper, spec->name, upper,
            upper, spec->name, upper,
            upper, spec->name, upper);
        if (has_object) {
            vmgen_buf_appendf(&buf,
                "#define %s_OBJ(v)    ((%s_value_t){ .type = %s_VAL_OBJECT, .as.obj = (void*)(v) })\n",
                upper, spec->name, upper);
        }
        vmgen_buf_appendc(&buf, '\n');
    } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
        vmgen_buf_appendf(&buf, "typedef double %s_value_t;\n\n", spec->name);
    } else {
        vmgen_buf_appendf(&buf, "typedef int64_t %s_value_t;\n\n", spec->name);
    }

    /* Constant pool */
    if (spec->pool.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "typedef struct {\n"
            "    %s_value_t *values;\n"
            "    int count;\n"
            "    int capacity;\n"
            "} %s_const_pool_t;\n\n",
            spec->name, spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "#define %s_MAX_CONSTANTS %d\n\n"
            "typedef struct {\n"
            "    %s_value_t values[%s_MAX_CONSTANTS];\n"
            "    int count;\n"
            "} %s_const_pool_t;\n\n",
            upper, spec->pool.initial_capacity,
            spec->name, upper, spec->name);
    }

    /* Call frame */
    if (spec->frame.type != VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "#define %s_MAX_FRAMES %d\n",
            upper, spec->frame.max_depth);
    }
    vmgen_buf_appendf(&buf,
        "#define %s_MAX_BREAKPOINTS 32\n\n"
        "typedef struct {\n"
        "    size_t return_ip;\n"
        "    int base_sp;\n"
        "    uint16_t func_index;\n"
        "} %s_frame_t;\n\n",
        upper, spec->name);

    /* Error handling */
    vmgen_buf_appendf(&buf,
        "/* Error codes */\n"
        "typedef enum {\n"
        "    %s_ERR_NONE = 0,\n"
        "    %s_ERR_STACK_OVERFLOW,\n"
        "    %s_ERR_STACK_UNDERFLOW,\n"
        "    %s_ERR_UNKNOWN_OPCODE,\n"
        "    %s_ERR_CONST_OVERFLOW,\n"
        "    %s_ERR_DIV_BY_ZERO,\n"
        "    %s_ERR_TYPE_MISMATCH,\n"
        "    %s_ERR_UNHANDLED_EXCEPTION,\n"
        "} %s_error_t;\n\n"
        "/* Error handler callback: receives VM pointer, error code, and message */\n"
        "typedef void (*%s_error_handler_t)(void *vm, %s_error_t err, const char *msg);\n\n",
        upper, upper, upper, upper, upper, upper, upper, upper,
        spec->name, spec->name, spec->name);

    /* VM state struct */
    if (spec->features.exception_handling) {
        vmgen_buf_appendf(&buf,
            "#define %s_MAX_EXCEPTION_FRAMES 16\n\n"
            "typedef struct {\n"
            "    size_t handler_ip;\n"
            "    int saved_sp;\n"
            "    int saved_frame_count;\n"
            "} %s_exception_frame_t;\n\n",
            upper, spec->name);
    }

    vmgen_buf_appendf(&buf,
        "typedef struct %s_vm {\n"
        "    const uint8_t *code;\n"
        "    size_t code_len;\n"
        "    size_t ip;\n",
        spec->name);

    /* Stack — dynamic or fixed */
    if (spec->stack.type == VMGEN_STACK_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "    %s_value_t *stack;\n"
            "    int sp;\n"
            "    int stack_capacity;\n",
            spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "    %s_value_t stack[%d];\n"
            "    int sp;\n",
            spec->name, spec->stack.max_depth);
    }

    vmgen_buf_appendf(&buf,
        "    int running;\n"
        "    %s_const_pool_t constants;\n",
        spec->name);

    /* Frames — dynamic or fixed */
    if (spec->frame.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "    %s_frame_t *frames;\n"
            "    int frame_count;\n"
            "    int frame_capacity;\n",
            spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "    %s_frame_t frames[%s_MAX_FRAMES];\n"
            "    int frame_count;\n",
            spec->name, upper);
    }

    if (spec->features.debug_tracing) {
        vmgen_buf_append(&buf, "    int trace_enabled;\n");
    }

    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf, "    %s_gc_t *gc;\n", spec->name);
    }

    vmgen_buf_appendf(&buf,
        "    %s_error_t last_error;\n"
        "    %s_error_handler_t error_handler;\n"
        "    void *error_userdata;\n"
        "    /* Debug stepping */\n"
        "    size_t breakpoints[%s_MAX_BREAKPOINTS];\n"
        "    int breakpoint_count;\n"
        "    void (*step_callback)(void *vm, size_t ip);\n",
        spec->name, spec->name, upper);

    if (spec->features.exception_handling) {
        vmgen_buf_appendf(&buf,
            "    /* Exception handling */\n"
            "    %s_exception_frame_t exception_frames[%s_MAX_EXCEPTION_FRAMES];\n"
            "    int exception_count;\n"
            "    %s_value_t current_exception;\n",
            spec->name, upper, spec->name);
    }

    /* User-defined extension fields */
    for (int i = 0; i < spec->extension_count; i++) {
        const vmgen_extension_t *ext = &spec->extensions[i];
        if (ext->comment[0]) {
            vmgen_buf_appendf(&buf, "    %s %s; /* %s */\n",
                ext->type_str, ext->name, ext->comment);
        } else {
            vmgen_buf_appendf(&buf, "    %s %s;\n",
                ext->type_str, ext->name);
        }
    }

    /* Cleanup callback */
    vmgen_buf_appendf(&buf,
        "    void (*cleanup)(struct %s_vm *vm);\n",
        spec->name);

    vmgen_buf_appendf(&buf, "} %s_vm_t;\n\n", spec->name);

    /* API prototypes */
    vmgen_buf_appendf(&buf,
        "void %s_vm_init(%s_vm_t *vm);\n"
        "void %s_vm_free(%s_vm_t *vm);\n"
        "void %s_vm_set_cleanup(%s_vm_t *vm, void (*cleanup)(%s_vm_t *vm));\n"
        "int  %s_vm_run(%s_vm_t *vm, const uint8_t *code, size_t code_len);\n"
        "void %s_vm_push(%s_vm_t *vm, %s_value_t value);\n"
        "%s_value_t %s_vm_pop(%s_vm_t *vm);\n"
        "int  %s_vm_add_constant(%s_vm_t *vm, %s_value_t value);\n"
        "void %s_vm_set_error_handler(%s_vm_t *vm, %s_error_handler_t handler, void *userdata);\n"
        "/* Debug stepping API */\n"
        "int  %s_vm_step(%s_vm_t *vm);\n"
        "int  %s_vm_set_breakpoint(%s_vm_t *vm, size_t offset);\n"
        "int  %s_vm_clear_breakpoint(%s_vm_t *vm, size_t offset);\n"
        "void %s_vm_set_step_callback(%s_vm_t *vm, void (*cb)(void *vm, size_t ip));\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    /* Exposed dispatch table declarations */
    if (spec->features.expose_dispatch_table &&
        (spec->dispatch == VMGEN_DISPATCH_FNPTR ||
         spec->dispatch == VMGEN_DISPATCH_AUTO)) {
        vmgen_buf_appendf(&buf,
            "\n/* Dispatch table (externally visible) */\n"
            "typedef int (*%s_handler_t)(%s_vm_t *vm);\n"
            "extern %s_handler_t %s_dispatch_table[];\n",
            spec->name, spec->name,
            spec->name, spec->name);
    }

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}

/* ---- Emit instruction action body ---- */

static void emit_action_body(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                             const vmgen_instr_t *instr, const char *indent) {
    if (instr->action_c && instr->action_c[0]) {
        /* Inline C: emit verbatim with indentation */
        vmgen_action_emit_inline_c(buf, indent, instr->action_c);
    } else if (instr->action && instr->action[0]) {
        /* DSL action: parse and emit */
        vmgen_action_t action;
        if (vmgen_action_parse(instr->action, &action) == 0) {
            /* Stack effect validation warning */
            if (instr->stack_effect_set) {
                int computed = vmgen_action_stack_effect(&action);
                if (computed != instr->stack_effect) {
                    fprintf(stderr,
                        "vmgen: warning: %s declared stack_effect=%d but computed=%d\n",
                        instr->name, instr->stack_effect, computed);
                }
            }
            vmgen_action_emit(buf, indent, spec, instr, &action);
            vmgen_action_free(&action);
        } else {
            fprintf(stderr, "vmgen: error parsing action for %s: %s\n",
                    instr->name, action.error);
            vmgen_buf_appendf(buf, "%s/* ERROR: failed to parse action: %s */\n",
                              indent, action.error);
        }
    } else {
        /* No action defined: emit TODO stub */
        vmgen_buf_appendf(buf, "%s/* TODO: implement %s */\n", indent, instr->name);
    }
}

/* Check if an instruction's action ends with halt (no break/return needed) */
static int action_has_terminal(const vmgen_instr_t *instr) {
    if (instr->action && instr->action[0]) {
        vmgen_action_t action;
        if (vmgen_action_parse(instr->action, &action) == 0 && action.count > 0) {
            vmgen_act_type_t last = action.stmts[action.count - 1].type;
            vmgen_action_free(&action);
            return last == ACT_HALT;
        }
        vmgen_action_free(&action);
    }
    return 0;
}

/* ---- Generate vm.c (switch dispatch) ---- */

static void emit_switch_dispatch(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                                 const char *upper) {
    vmgen_buf_append(buf,
        "    while (vm->running && vm->ip < vm->code_len) {\n"
        "        uint8_t opcode = vm->code[vm->ip++];\n");

    if (spec->features.debug_tracing) {
        vmgen_buf_appendf(buf,
            "        if (vm->trace_enabled) {\n"
            "            %s_disasm_instruction(vm->code, vm->code_len, vm->ip - 1);\n"
            "        }\n",
            spec->name);
    }

    vmgen_buf_append(buf,
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

        /* Emit action body */
        if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP) {
            vmgen_buf_appendf(buf, "            if (vm->gc) %s_gc_inhibit(vm->gc);\n", spec->name);
        }
        emit_action_body(buf, spec, instr, "            ");
        if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP && !action_has_terminal(instr)) {
            vmgen_buf_appendf(buf, "            if (vm->gc) %s_gc_allow(vm->gc);\n", spec->name);
        }
        if (!action_has_terminal(instr)) {
            vmgen_buf_append(buf, "            break;\n");
        }
        vmgen_buf_append(buf, "        }\n");
    }

    vmgen_buf_appendf(buf,
        "        default: {\n"
        "            char msg[64];\n"
        "            snprintf(msg, sizeof(msg), \"unknown opcode %%d at ip %%zu\",\n"
        "                     opcode, vm->ip - 1);\n"
        "            %s_vm_error(vm, %s_ERR_UNKNOWN_OPCODE, msg);\n"
        "            return -1;\n"
        "        }\n"
        "        }\n"
        "    }\n",
        spec->name, upper);
}


/* ---- Generate vm.c (threaded dispatch via computed gotos) ---- */

static void emit_threaded_dispatch(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                                   const char *upper) {
    /* Label dispatch table */
    vmgen_buf_append(buf,
        "    static void *dispatch_table[] = {\n");
    for (int i = 0; i < spec->instruction_count; i++) {
        vmgen_buf_appendf(buf, "        &&op_%s,\n", spec->instructions[i].name);
    }
    vmgen_buf_append(buf, "    };\n\n");

    vmgen_buf_append(buf,
        "#define DISPATCH() goto *dispatch_table[vm->code[vm->ip++]]\n\n"
        "    DISPATCH();\n\n");

    /* Emit labeled blocks */
    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(buf, "    op_%s: {\n", instr->name);

        if (spec->features.debug_tracing) {
            vmgen_buf_appendf(buf,
                "        if (vm->trace_enabled) {\n"
                "            %s_disasm_instruction(vm->code, vm->code_len, vm->ip - 1);\n"
                "        }\n",
                spec->name);
        }

        /* Decode operands */
        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(buf, "        %s %s = %s;\n",
                              c_type_for_operand(instr->operands[j].type),
                              instr->operands[j].name,
                              read_macro_for(instr->operands[j].type));
        }

        /* GC inhibit + action body + GC allow */
        bool terminal = action_has_terminal(instr) != 0;
        if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP) {
            vmgen_buf_appendf(buf, "        if (vm->gc) %s_gc_inhibit(vm->gc);\n", spec->name);
        }
        emit_action_body(buf, spec, instr, "        ");
        if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP && !terminal) {
            vmgen_buf_appendf(buf, "        if (vm->gc) %s_gc_allow(vm->gc);\n", spec->name);
        }

        if (!terminal) {
            vmgen_buf_append(buf, "        DISPATCH();\n");
        }
        vmgen_buf_append(buf, "    }\n\n");
    }

    vmgen_buf_append(buf, "#undef DISPATCH\n");

    (void)upper;
}
static void emit_fnptr_dispatch(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                                const char *upper) {
    int exposed = spec->features.expose_dispatch_table;
    const char *linkage = exposed ? "" : "static ";

    /* Forward-declare handler type (in .c only when not exposed — header has it) */
    if (!exposed) {
        vmgen_buf_appendf(buf,
            "typedef int (*%s_handler_t)(%s_vm_t *vm);\n\n",
            spec->name, spec->name);
    }

    /* Generate handler functions */
    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(buf,
            "%sint handle_%s(%s_vm_t *vm) {\n",
            linkage, instr->name, spec->name);

        /* Decode operands */
        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(buf, "    %s %s = %s;\n",
                              c_type_for_operand(instr->operands[j].type),
                              instr->operands[j].name,
                              read_macro_for(instr->operands[j].type));
        }

        /* Emit action body */
        bool has_action = (instr->action && instr->action[0]) ||
                          (instr->action_c && instr->action_c[0]);
        bool terminal = action_has_terminal(instr);
        bool uses_vm = has_action || instr->operand_count > 0 || spec->features.gc_enabled;
        if (!uses_vm) {
            vmgen_buf_append(buf, "    (void)vm;\n");
        }
        if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP) {
            vmgen_buf_appendf(buf, "    if (vm->gc) %s_gc_inhibit(vm->gc);\n", spec->name);
        }
        emit_action_body(buf, spec, instr, "    ");
        if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP && !terminal) {
            vmgen_buf_appendf(buf, "    if (vm->gc) %s_gc_allow(vm->gc);\n", spec->name);
        }

        if (!terminal) {
            vmgen_buf_append(buf, "    return 0;\n");
        }
        vmgen_buf_append(buf, "}\n\n");
    }

    /* Dispatch table */
    vmgen_buf_appendf(buf,
        "%s%s_handler_t %s_dispatch_table[] = {\n",
        linkage, spec->name, spec->name);
    for (int i = 0; i < spec->instruction_count; i++) {
        vmgen_buf_appendf(buf, "    handle_%s,\n",
                          spec->instructions[i].name);
    }
    vmgen_buf_append(buf, "};\n\n");

    (void)upper;
}

static void emit_fnptr_loop(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                            const char *upper) {
    vmgen_buf_appendf(buf,
        "    while (vm->running && vm->ip < vm->code_len) {\n"
        "        uint8_t opcode = vm->code[vm->ip++];\n");

    if (spec->features.debug_tracing) {
        vmgen_buf_appendf(buf,
            "        if (vm->trace_enabled) {\n"
            "            %s_disasm_instruction(vm->code, vm->code_len, vm->ip - 1);\n"
            "        }\n",
            spec->name);
    }

    vmgen_buf_appendf(buf,
        "        if (opcode >= %s_OP_COUNT) {\n"
        "            char msg[64];\n"
        "            snprintf(msg, sizeof(msg), \"unknown opcode %%d at ip %%zu\",\n"
        "                     opcode, vm->ip - 1);\n"
        "            %s_vm_error(vm, %s_ERR_UNKNOWN_OPCODE, msg);\n"
        "            return -1;\n"
        "        }\n"
        "        int rc = %s_dispatch_table[opcode](vm);\n"
        "        if (rc != 0) return rc;\n"
        "    }\n",
        upper, spec->name, upper, spec->name);
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
        "#include <stdlib.h>\n"
        "#include <string.h>\n");

    if (spec->features.debug_tracing) {
        vmgen_buf_append(&buf, "#include \"disasm.h\"\n");
    }

    vmgen_buf_append(&buf, "\n");

    emit_read_macros(&buf);

    /* Default error handler and helper */
    vmgen_buf_appendf(&buf,
        "static void %s_default_error_handler(void *vm, %s_error_t err, const char *msg) {\n"
        "    (void)vm; (void)err;\n"
        "    fprintf(stderr, \"%%s\\n\", msg);\n"
        "}\n\n"
        "static void %s_vm_error(%s_vm_t *vm, %s_error_t err, const char *msg) {\n"
        "    vm->last_error = err;\n"
        "    vm->running = 0;\n"
        "    if (vm->error_handler) {\n"
        "        vm->error_handler(vm->error_userdata ? vm->error_userdata : vm, err, msg);\n"
        "    } else {\n"
        "        %s_default_error_handler(vm, err, msg);\n"
        "    }\n"
        "}\n\n"
        "void %s_vm_set_error_handler(%s_vm_t *vm, %s_error_handler_t handler, void *userdata) {\n"
        "    vm->error_handler = handler;\n"
        "    vm->error_userdata = userdata;\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name,
        spec->name, spec->name, spec->name);

    /* Stack operations */
    if (spec->stack.type == VMGEN_STACK_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "static void %s_vm_ensure_stack(%s_vm_t *vm, int needed) {\n"
            "    if (needed <= vm->stack_capacity) return;\n"
            "    int new_cap = vm->stack_capacity * 2;\n"
            "    if (new_cap < needed) new_cap = needed;\n"
            "    %s_value_t *tmp = (%s_value_t *)realloc(vm->stack,\n"
            "        (size_t)new_cap * sizeof(%s_value_t));\n"
            "    if (!tmp) {\n"
            "        %s_vm_error(vm, %s_ERR_STACK_OVERFLOW, \"stack realloc failed\");\n"
            "        return;\n"
            "    }\n"
            "    vm->stack = tmp;\n"
            "    vm->stack_capacity = new_cap;\n"
            "}\n\n"
            "void %s_vm_push(%s_vm_t *vm, %s_value_t value) {\n"
            "    %s_vm_ensure_stack(vm, vm->sp + 1);\n"
            "    vm->stack[vm->sp++] = value;\n"
            "}\n\n",
            spec->name, spec->name,
            spec->name, spec->name, spec->name,
            spec->name, upper,
            spec->name, spec->name, spec->name,
            spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "void %s_vm_push(%s_vm_t *vm, %s_value_t value) {\n"
            "    if (vm->sp >= %d) {\n"
            "        %s_vm_error(vm, %s_ERR_STACK_OVERFLOW, \"stack overflow\");\n"
            "        return;\n"
            "    }\n"
            "    vm->stack[vm->sp++] = value;\n"
            "}\n\n",
            spec->name, spec->name, spec->name, spec->stack.max_depth,
            spec->name, upper);
    }

    vmgen_buf_appendf(&buf,
        "%s_value_t %s_vm_pop(%s_vm_t *vm) {\n"
        "    if (vm->sp <= 0) {\n"
        "        %s_vm_error(vm, %s_ERR_STACK_UNDERFLOW, \"stack underflow\");\n",
        spec->name, spec->name, spec->name,
        spec->name, upper);

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
        "    memset(vm, 0, sizeof(*vm));\n",
        spec->name, spec->name);

    if (spec->stack.type == VMGEN_STACK_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "    vm->stack_capacity = %d;\n"
            "    vm->stack = (%s_value_t *)calloc((size_t)vm->stack_capacity, sizeof(%s_value_t));\n",
            spec->stack.max_depth, spec->name, spec->name);
    }
    if (spec->pool.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "    vm->constants.capacity = %d;\n"
            "    vm->constants.values = (%s_value_t *)calloc((size_t)vm->constants.capacity, sizeof(%s_value_t));\n",
            spec->pool.initial_capacity, spec->name, spec->name);
    }
    if (spec->frame.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "    vm->frame_capacity = %d;\n"
            "    vm->frames = (%s_frame_t *)calloc((size_t)vm->frame_capacity, sizeof(%s_frame_t));\n",
            spec->frame.max_depth, spec->name, spec->name);
    }

    vmgen_buf_append(&buf, "}\n\n");

    /* add_constant */
    if (spec->pool.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_appendf(&buf,
            "int %s_vm_add_constant(%s_vm_t *vm, %s_value_t value) {\n"
            "    if (vm->constants.count >= vm->constants.capacity) {\n"
            "        int new_cap = vm->constants.capacity * 2;\n"
            "        if (new_cap < 16) new_cap = 16;\n"
            "        %s_value_t *tmp = (%s_value_t *)realloc(vm->constants.values,\n"
            "            (size_t)new_cap * sizeof(%s_value_t));\n"
            "        if (!tmp) {\n"
            "            %s_vm_error(vm, %s_ERR_CONST_OVERFLOW, \"constant pool realloc failed\");\n"
            "            return -1;\n"
            "        }\n"
            "        vm->constants.values = tmp;\n"
            "        vm->constants.capacity = new_cap;\n"
            "    }\n"
            "    int index = vm->constants.count;\n"
            "    vm->constants.values[vm->constants.count++] = value;\n"
            "    return index;\n"
            "}\n\n",
            spec->name, spec->name, spec->name,
            spec->name, spec->name, spec->name,
            spec->name, upper);
    } else {
        vmgen_buf_appendf(&buf,
            "int %s_vm_add_constant(%s_vm_t *vm, %s_value_t value) {\n"
            "    if (vm->constants.count >= %s_MAX_CONSTANTS) {\n"
            "        %s_vm_error(vm, %s_ERR_CONST_OVERFLOW, \"constant pool overflow\");\n"
            "        return -1;\n"
            "    }\n"
            "    int index = vm->constants.count;\n"
            "    vm->constants.values[vm->constants.count++] = value;\n"
            "    return index;\n"
            "}\n\n",
            spec->name, spec->name, spec->name, upper,
            spec->name, upper);
    }

    /* vm_free */
    vmgen_buf_appendf(&buf,
        "void %s_vm_free(%s_vm_t *vm) {\n"
        "    if (vm->cleanup) vm->cleanup(vm);\n",
        spec->name, spec->name);

    if (spec->stack.type == VMGEN_STACK_DYNAMIC) {
        vmgen_buf_append(&buf, "    free(vm->stack);\n");
    }
    if (spec->pool.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_append(&buf, "    free(vm->constants.values);\n");
    }
    if (spec->frame.type == VMGEN_ALLOC_DYNAMIC) {
        vmgen_buf_append(&buf, "    free(vm->frames);\n");
    }

    vmgen_buf_append(&buf,
        "    memset(vm, 0, sizeof(*vm));\n"
        "}\n\n");

    /* vm_set_cleanup */
    vmgen_buf_appendf(&buf,
        "void %s_vm_set_cleanup(%s_vm_t *vm, void (*cleanup)(%s_vm_t *vm)) {\n"
        "    vm->cleanup = cleanup;\n"
        "}\n\n",
        spec->name, spec->name, spec->name);

    /* GC root scanning function (mark-sweep only) */
    if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP) {
        vmgen_buf_appendf(&buf,
            "static void %s_vm_mark_roots(void *data) {\n"
            "    %s_vm_t *vm = (%s_vm_t *)data;\n",
            spec->name, spec->name, spec->name);

        if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
            vmgen_buf_appendf(&buf,
                "    /* Mark object references on the stack */\n"
                "    for (int i = 0; i < vm->sp; i++) {\n"
                "        if (vm->stack[i].type == %s_VAL_OBJECT && vm->stack[i].as.obj) {\n"
                "            %s_gc_mark_object(vm->stack[i].as.obj);\n"
                "        }\n"
                "    }\n"
                "    /* Mark object references in the constant pool */\n"
                "    for (int i = 0; i < vm->constants.count; i++) {\n"
                "        if (vm->constants.values[i].type == %s_VAL_OBJECT &&\n"
                "            vm->constants.values[i].as.obj) {\n"
                "            %s_gc_mark_object(vm->constants.values[i].as.obj);\n"
                "        }\n"
                "    }\n",
                upper, spec->name,
                upper, spec->name);
        } else {
            vmgen_buf_append(&buf,
                "    /* Non-tagged-union value type: no embedded object references */\n"
                "    (void)vm;\n");
        }

        vmgen_buf_append(&buf, "}\n\n");
    }

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
        "    vm->running = 1;\n",
        spec->name, spec->name);

    if (spec->features.gc_enabled && spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP) {
        vmgen_buf_appendf(&buf,
            "    if (vm->gc) {\n"
            "        %s_gc_set_roots(vm->gc, %s_vm_mark_roots, vm);\n"
            "    }\n",
            spec->name, spec->name);
    }

    vmgen_buf_appendc(&buf, '\n');

    switch (spec->dispatch) {
    case VMGEN_DISPATCH_SWITCH:
        emit_switch_dispatch(&buf, spec, upper);
        break;
    case VMGEN_DISPATCH_FNPTR:
        emit_fnptr_loop(&buf, spec, upper);
        break;
    case VMGEN_DISPATCH_AUTO:
        vmgen_buf_append(&buf, "#if defined(__GNUC__) || defined(__clang__)\n");
        emit_threaded_dispatch(&buf, spec, upper);
        vmgen_buf_append(&buf, "#else\n");
        emit_fnptr_loop(&buf, spec, upper);
        vmgen_buf_append(&buf, "#endif\n");
        break;
    case VMGEN_DISPATCH_THREADED:
        vmgen_buf_append(&buf, "#if defined(__GNUC__) || defined(__clang__)\n");
        emit_threaded_dispatch(&buf, spec, upper);
        vmgen_buf_append(&buf, "#else\n");
        vmgen_buf_append(&buf, "#error \"Threaded dispatch requires GCC or Clang\"\n");
        vmgen_buf_append(&buf, "#endif\n");
        break;
    }

    vmgen_buf_append(&buf, "    return 0;\n}\n\n");

    /* Debug stepping: vm_step — execute one instruction */
    vmgen_buf_appendf(&buf,
        "int %s_vm_step(%s_vm_t *vm) {\n"
        "    if (!vm->running || vm->ip >= vm->code_len) return -1;\n"
        "    if (vm->step_callback) vm->step_callback(vm, vm->ip);\n"
        "    uint8_t opcode = vm->code[vm->ip++];\n",
        spec->name, spec->name);

    /* Emit a switch for single-step that handles one opcode */
    vmgen_buf_append(&buf, "    switch (opcode) {\n");
    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(&buf, "    case %s_OP_%s: {\n", upper, instr->name);

        /* Decode operands */
        for (int j = 0; j < instr->operand_count; j++) {
            const vmgen_operand_t *op = &instr->operands[j];
            if (op->type == VMGEN_OPERAND_UINT8) {
                vmgen_buf_appendf(&buf, "        uint8_t %s = READ_UINT8();\n", op->name);
            } else if (op->type == VMGEN_OPERAND_UINT16) {
                vmgen_buf_appendf(&buf, "        uint16_t %s = READ_UINT16();\n", op->name);
            } else if (op->type == VMGEN_OPERAND_INT16) {
                vmgen_buf_appendf(&buf, "        int16_t %s = (int16_t)READ_UINT16();\n", op->name);
            }
        }

        emit_action_body(&buf, spec, instr, "        ");
        if (!action_has_terminal(instr)) {
            vmgen_buf_append(&buf, "        break;\n");
        }
        vmgen_buf_append(&buf, "    }\n");
    }
    vmgen_buf_appendf(&buf,
        "    default: {\n"
        "        char msg[64];\n"
        "        snprintf(msg, sizeof(msg), \"unknown opcode %%d at ip %%zu\",\n"
        "                 opcode, vm->ip - 1);\n"
        "        %s_vm_error(vm, %s_ERR_UNKNOWN_OPCODE, msg);\n"
        "        return -1;\n"
        "    }\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n",
        spec->name, upper);

    /* Breakpoint management */
    vmgen_buf_appendf(&buf,
        "int %s_vm_set_breakpoint(%s_vm_t *vm, size_t offset) {\n"
        "    if (vm->breakpoint_count >= %s_MAX_BREAKPOINTS) return -1;\n"
        "    vm->breakpoints[vm->breakpoint_count++] = offset;\n"
        "    return 0;\n"
        "}\n\n"
        "int %s_vm_clear_breakpoint(%s_vm_t *vm, size_t offset) {\n"
        "    for (int i = 0; i < vm->breakpoint_count; i++) {\n"
        "        if (vm->breakpoints[i] == offset) {\n"
        "            vm->breakpoints[i] = vm->breakpoints[--vm->breakpoint_count];\n"
        "            return 0;\n"
        "        }\n"
        "    }\n"
        "    return -1;\n"
        "}\n\n"
        "void %s_vm_set_step_callback(%s_vm_t *vm, void (*cb)(void *vm, size_t ip)) {\n"
        "    vm->step_callback = cb;\n"
        "}\n",
        spec->name, spec->name, upper,
        spec->name, spec->name,
        spec->name, spec->name);

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
