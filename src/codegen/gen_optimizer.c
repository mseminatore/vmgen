#include "gen_optimizer.h"
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

/* Find instruction index by name, or -1 */
static int find_instr(const vmgen_spec_t *spec, const char *name) {
    for (int i = 0; i < spec->instruction_count; i++) {
        if (strcmp(spec->instructions[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* Check if action string matches "pop b; pop a; push a <op> b" pattern */
static int is_binary_arith(const vmgen_instr_t *instr) {
    if (!instr->action) return 0;
    return (strstr(instr->action, "pop") != NULL &&
            strstr(instr->action, "push") != NULL &&
            (strstr(instr->action, " + ") != NULL ||
             strstr(instr->action, " - ") != NULL ||
             strstr(instr->action, " * ") != NULL ||
             strstr(instr->action, " / ") != NULL));
}

/* Check if action is terminal (halt or return) */
static int is_terminal(const vmgen_instr_t *instr) {
    if (!instr->action) return 0;
    return (strcmp(instr->action, "halt") == 0 ||
            strcmp(instr->action, "return") == 0);
}

/* Get the C arithmetic operator for an action like "pop b; pop a; push a + b" */
static char get_arith_op(const vmgen_instr_t *instr) {
    if (!instr->action) return 0;
    if (strstr(instr->action, " + ")) return '+';
    if (strstr(instr->action, " - ")) return '-';
    if (strstr(instr->action, " * ")) return '*';
    if (strstr(instr->action, " / ")) return '/';
    return 0;
}

int vmgen_gen_optimizer(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    int push_const_idx = find_instr(spec, "PUSH_CONST");
    int pop_idx = find_instr(spec, "POP");

    /* Collect binary arithmetic opcodes */
    int arith_opcodes[VMGEN_MAX_INSTR];
    char arith_ops[VMGEN_MAX_INSTR];
    int arith_count = 0;
    for (int i = 0; i < spec->instruction_count; i++) {
        if (is_binary_arith(&spec->instructions[i])) {
            arith_opcodes[arith_count] = i;
            arith_ops[arith_count] = get_arith_op(&spec->instructions[i]);
            arith_count++;
        }
    }

    /* Collect terminal opcodes */
    int terminal_opcodes[VMGEN_MAX_INSTR];
    int terminal_count = 0;
    for (int i = 0; i < spec->instruction_count; i++) {
        if (is_terminal(&spec->instructions[i])) {
            terminal_opcodes[terminal_count++] = i;
        }
    }

    /* --- optimizer.h --- */
    vmgen_buf_t hdr;
    vmgen_buf_init(&hdr);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_OPTIMIZER_H", upper);
    vmgen_emit_header(&hdr, "optimizer.h", spec);
    vmgen_emit_guard_open(&hdr, guard);

    vmgen_buf_appendf(&hdr,
        "#include \"opcodes.h\"\n"
        "#include \"vm.h\"\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "/* NOP sentinel: replaced instructions become this byte */\n"
        "#define %s_OP_NOP 0xFE\n\n"
        "/* Optimization statistics */\n"
        "typedef struct {\n"
        "    int constants_folded;\n"
        "    int dead_code_bytes;\n"
        "    int peephole_opts;\n"
        "    int nops_stripped;\n"
        "} %s_opt_stats_t;\n\n"
        "/* Run all optimization passes on bytecode in-place.\n"
        " * NOP-fills replaced instructions (no offset changes).\n"
        " * Returns number of optimizations applied. */\n"
        "int %s_optimize(uint8_t *code, size_t code_len,\n"
        "                %s_const_pool_t *constants,\n"
        "                %s_opt_stats_t *stats);\n\n"
        "/* Strip NOP bytes and patch jump offsets.\n"
        " * Returns the new (smaller) code length. */\n"
        "size_t %s_optimize_strip_nops(uint8_t *code, size_t code_len);\n",
        upper,
        spec->name,
        spec->name,
        spec->name,
        spec->name,
        spec->name);

    vmgen_emit_guard_close(&hdr, guard);

    char path[512];
    snprintf(path, sizeof(path), "%s/optimizer.h", output_dir);
    int rc = vmgen_buf_write_file(&hdr, path);
    vmgen_buf_free(&hdr);
    if (rc != 0) return rc;

    /* --- optimizer.c --- */
    vmgen_buf_t src;
    vmgen_buf_init(&src);
    vmgen_emit_header(&src, "optimizer.c", spec);

    vmgen_buf_appendf(&src,
        "#include \"optimizer.h\"\n"
        "#include <string.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n\n");

    /* Helper: read uint16 LE */
    vmgen_buf_appendf(&src,
        "static uint16_t read_u16(const uint8_t *p) {\n"
        "    return (uint16_t)(p[0] | (p[1] << 8));\n"
        "}\n\n"
        "static void write_u16(uint8_t *p, uint16_t val) {\n"
        "    p[0] = (uint8_t)(val & 0xFF);\n"
        "    p[1] = (uint8_t)((val >> 8) & 0xFF);\n"
        "}\n\n");

    /* NOP fill helper */
    vmgen_buf_appendf(&src,
        "static void nop_fill(uint8_t *code, size_t start, size_t count) {\n"
        "    for (size_t i = 0; i < count; i++)\n"
        "        code[start + i] = %s_OP_NOP;\n"
        "}\n\n",
        upper);

    /* --- Constant folding pass --- */
    if (push_const_idx >= 0 && arith_count > 0) {
        vmgen_buf_appendf(&src,
            "/* Constant folding: PUSH_CONST X, PUSH_CONST Y, <arith> → PUSH_CONST Z */\n"
            "static int fold_constants(uint8_t *code, size_t code_len,\n"
            "                          %s_const_pool_t *constants) {\n"
            "    int folded = 0;\n"
            "    size_t ip = 0;\n"
            "    while (ip + 6 < code_len) {\n"
            "        /* Match: PUSH_CONST <u16>, PUSH_CONST <u16>, <arith> */\n"
            "        if (code[ip] != %s_OP_PUSH_CONST ||\n"
            "            code[ip + 3] != %s_OP_PUSH_CONST) {\n"
            "            ip += %s_instruction_sizes[code[ip] < %s_OP_COUNT ? code[ip] : 0];\n"
            "            continue;\n"
            "        }\n"
            "        uint8_t arith_op = code[ip + 6];\n",
            spec->name, upper, upper, spec->name, upper);

        /* Check if the third byte is an arithmetic opcode */
        vmgen_buf_append(&src, "        int64_t result = 0;\n");
        vmgen_buf_append(&src, "        int matched = 0;\n");

        for (int i = 0; i < arith_count; i++) {
            const char *elif = (i == 0) ? "if" : "} else if";
            vmgen_buf_appendf(&src,
                "        %s (arith_op == %s_OP_%s) {\n"
                "            uint16_t idx_a = read_u16(&code[ip + 1]);\n"
                "            uint16_t idx_b = read_u16(&code[ip + 4]);\n"
                "            if (idx_a >= constants->count || idx_b >= constants->count)\n"
                "                { ip += 3; continue; }\n",
                elif, upper, spec->instructions[arith_opcodes[i]].name);

            /* Emit the arithmetic operation based on value type */
            if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
                vmgen_buf_appendf(&src,
                    "            int64_t a = constants->values[idx_a].as.i64;\n"
                    "            int64_t b = constants->values[idx_b].as.i64;\n");
            } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
                vmgen_buf_appendf(&src,
                    "            double a = constants->values[idx_a];\n"
                    "            double b = constants->values[idx_b];\n");
            } else {
                vmgen_buf_appendf(&src,
                    "            int64_t a = constants->values[idx_a];\n"
                    "            int64_t b = constants->values[idx_b];\n");
            }

            char op = arith_ops[i];
            if (op == '/') {
                vmgen_buf_appendf(&src,
                    "            if (b == 0) { ip += 3; continue; }\n"
                    "            result = a %c b;\n"
                    "            matched = 1;\n", op);
            } else {
                vmgen_buf_appendf(&src,
                    "            result = a %c b;\n"
                    "            matched = 1;\n", op);
            }
        }
        vmgen_buf_append(&src, "        }\n");

        /* Rewrite if matched */
        if (spec->pool.type == VMGEN_ALLOC_DYNAMIC) {
            vmgen_buf_append(&src,
                "        if (matched && constants->count < constants->capacity) {\n"
                "            int new_idx = constants->count;\n");
        } else {
            vmgen_buf_appendf(&src,
                "        if (matched && constants->count < %s_MAX_CONSTANTS) {\n"
                "            int new_idx = constants->count;\n",
                upper);
        }

        if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
            vmgen_buf_appendf(&src,
                "            constants->values[new_idx] = %s_INT64(result);\n",
                upper);
        } else {
            vmgen_buf_append(&src,
                "            constants->values[new_idx] = result;\n");
        }

        /* Total size: PUSH_CONST(3) + PUSH_CONST(3) + arith(1) = 7 bytes → PUSH_CONST(3) + 4 NOPs */
        vmgen_buf_appendf(&src,
            "            constants->count++;\n"
            "            code[ip] = %s_OP_PUSH_CONST;\n"
            "            write_u16(&code[ip + 1], (uint16_t)new_idx);\n"
            "            nop_fill(code, ip + 3, 4);\n"
            "            folded++;\n"
            "            ip += 7;\n"
            "        } else {\n"
            "            ip += 3;\n"
            "        }\n"
            "    }\n"
            "    return folded;\n"
            "}\n\n",
            upper);
    }

    /* --- Dead code elimination pass --- */
    if (terminal_count > 0) {
        vmgen_buf_appendf(&src,
            "/* Dead code: NOP-fill bytes after HALT/RETURN until next jump target */\n"
            "static int eliminate_dead_code(uint8_t *code, size_t code_len) {\n"
            "    int eliminated = 0;\n"
            "    size_t ip = 0;\n"
            "    while (ip < code_len) {\n"
            "        uint8_t opcode = code[ip];\n"
            "        if (opcode == %s_OP_NOP) { ip++; continue; }\n"
            "        if (opcode >= %s_OP_COUNT) { ip++; continue; }\n"
            "        int sz = %s_instruction_sizes[opcode];\n"
            "        int is_term = ",
            upper, upper, spec->name);

        /* Check if opcode is terminal */
        for (int i = 0; i < terminal_count; i++) {
            if (i > 0) vmgen_buf_append(&src, " || ");
            vmgen_buf_appendf(&src, "opcode == %s_OP_%s",
                              upper, spec->instructions[terminal_opcodes[i]].name);
        }
        vmgen_buf_appendf(&src, ";\n"
            "        ip += (size_t)sz;\n"
            "        if (is_term) {\n"
            "            while (ip < code_len && code[ip] != %s_OP_NOP) {\n"
            "                uint8_t next = code[ip];\n"
            "                if (next >= %s_OP_COUNT) break;\n"
            "                int nsz = %s_instruction_sizes[next];\n"
            "                nop_fill(code, ip, (size_t)nsz);\n"
            "                eliminated += nsz;\n"
            "                ip += (size_t)nsz;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    return eliminated;\n"
            "}\n\n",
            upper, upper, spec->name);
    }

    /* --- Peephole pass --- */
    if (push_const_idx >= 0 && pop_idx >= 0) {
        vmgen_buf_appendf(&src,
            "/* Peephole: PUSH_CONST + POP → 2 NOPs (useless push-then-discard) */\n"
            "static int peephole(uint8_t *code, size_t code_len) {\n"
            "    int optimized = 0;\n"
            "    size_t ip = 0;\n"
            "    while (ip + 3 < code_len) {\n"
            "        if (code[ip] == %s_OP_PUSH_CONST &&\n"
            "            code[ip + 3] == %s_OP_POP) {\n"
            "            nop_fill(code, ip, 4);\n"
            "            optimized++;\n"
            "            ip += 4;\n"
            "        } else {\n"
            "            uint8_t op = code[ip];\n"
            "            if (op == %s_OP_NOP) { ip++; continue; }\n"
            "            if (op >= %s_OP_COUNT) { ip++; continue; }\n"
            "            ip += (size_t)%s_instruction_sizes[op];\n"
            "        }\n"
            "    }\n"
            "    return optimized;\n"
            "}\n\n",
            upper, upper, upper, upper, spec->name);
    }

    /* --- Main optimize function --- */
    vmgen_buf_appendf(&src,
        "int %s_optimize(uint8_t *code, size_t code_len,\n"
        "                %s_const_pool_t *constants,\n"
        "                %s_opt_stats_t *stats) {\n"
        "    %s_opt_stats_t s;\n"
        "    memset(&s, 0, sizeof(s));\n"
        "    /* Not every pass is generated for every spec (e.g. no constant\n"
        "     * folding when arithmetic uses typed_action), so mark the shared\n"
        "     * helpers and parameters as used to keep the output warning-free. */\n"
        "    (void)constants;\n"
        "    (void)read_u16;\n"
        "    (void)write_u16;\n"
        "    (void)nop_fill;\n\n",
        spec->name, spec->name, spec->name, spec->name);

    if (push_const_idx >= 0 && arith_count > 0) {
        vmgen_buf_append(&src,
            "    /* Pass 1: Constant folding (iterate until no more folds) */\n"
            "    int folded;\n"
            "    do {\n"
            "        folded = fold_constants(code, code_len, constants);\n"
            "        s.constants_folded += folded;\n"
            "    } while (folded > 0);\n\n");
    }

    if (terminal_count > 0) {
        vmgen_buf_append(&src,
            "    /* Pass 2: Dead code elimination */\n"
            "    s.dead_code_bytes = eliminate_dead_code(code, code_len);\n\n");
    }

    if (push_const_idx >= 0 && pop_idx >= 0) {
        vmgen_buf_append(&src,
            "    /* Pass 3: Peephole optimization */\n"
            "    s.peephole_opts = peephole(code, code_len);\n\n");
    }

    vmgen_buf_append(&src,
        "    if (stats) *stats = s;\n"
        "    return s.constants_folded + s.dead_code_bytes + s.peephole_opts;\n"
        "}\n\n");

    /* --- NOP stripping pass --- */
    vmgen_buf_appendf(&src,
        "size_t %s_optimize_strip_nops(uint8_t *code, size_t code_len) {\n"
        "    /* Build offset map: old_offset → new_offset */\n"
        "    size_t *offmap = (size_t *)malloc(code_len * sizeof(size_t));\n"
        "    if (!offmap) return code_len;\n\n"
        "    /* First pass: compute new offsets */\n"
        "    size_t new_len = 0;\n"
        "    for (size_t i = 0; i < code_len; i++) {\n"
        "        offmap[i] = new_len;\n"
        "        if (code[i] != %s_OP_NOP) {\n"
        "            uint8_t op = code[i];\n"
        "            int sz = (op < %s_OP_COUNT) ? %s_instruction_sizes[op] : 1;\n"
        "            for (int j = 0; j < sz && (i + (size_t)j) < code_len; j++)\n"
        "                offmap[i + (size_t)j] = new_len + (size_t)j;\n"
        "            new_len += (size_t)sz;\n"
        "            i += (size_t)(sz - 1);\n"
        "        }\n"
        "    }\n\n",
        spec->name, upper, upper, spec->name);

    /* Second pass: compact and patch jumps */
    vmgen_buf_appendf(&src,
        "    /* Second pass: compact bytecode */\n"
        "    uint8_t *tmp = (uint8_t *)malloc(new_len);\n"
        "    if (!tmp) { free(offmap); return code_len; }\n"
        "    size_t dst = 0;\n"
        "    for (size_t i = 0; i < code_len; ) {\n"
        "        if (code[i] == %s_OP_NOP) { i++; continue; }\n"
        "        uint8_t op = code[i];\n"
        "        int sz = (op < %s_OP_COUNT) ? %s_instruction_sizes[op] : 1;\n"
        "        for (int j = 0; j < sz && (i + (size_t)j) < code_len; j++)\n"
        "            tmp[dst++] = code[i + (size_t)j];\n"
        "        i += (size_t)sz;\n"
        "    }\n\n",
        upper, upper, spec->name);

    /* Patch jump offsets */
    vmgen_buf_append(&src,
        "    /* Third pass: patch jump offsets in compacted code */\n"
        "    size_t ip = 0;\n"
        "    while (ip < new_len) {\n"
        "        uint8_t op = tmp[ip];\n");

    /* Find jump instructions and patch their offsets */
    int has_jumps = 0;
    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        for (int j = 0; j < instr->operand_count; j++) {
            if (strcmp(instr->operands[j].name, "offset") == 0 &&
                instr->operands[j].type == VMGEN_OPERAND_INT16) {
                has_jumps = 1;
                /* Calculate operand position within instruction */
                int op_offset = 1;
                for (int k = 0; k < j; k++) {
                    if (instr->operands[k].type == VMGEN_OPERAND_UINT8) op_offset += 1;
                    else if (instr->operands[k].type == VMGEN_OPERAND_UINT16 ||
                             instr->operands[k].type == VMGEN_OPERAND_INT16) op_offset += 2;
                    else op_offset += 4;
                }
                int instr_size = 1;
                for (int k = 0; k < instr->operand_count; k++) {
                    if (instr->operands[k].type == VMGEN_OPERAND_UINT8) instr_size += 1;
                    else if (instr->operands[k].type == VMGEN_OPERAND_UINT16 ||
                             instr->operands[k].type == VMGEN_OPERAND_INT16) instr_size += 2;
                    else instr_size += 4;
                }

                vmgen_buf_appendf(&src,
                    "        if (op == %s_OP_%s) {\n"
                    "            int16_t old_off = (int16_t)read_u16(&tmp[ip + %d]);\n"
                    "            /* Find original offset for this ip */\n"
                    "            size_t orig_ip = 0;\n"
                    "            for (size_t k = 0; k < code_len; k++) {\n"
                    "                if (offmap[k] == ip) { orig_ip = k; break; }\n"
                    "            }\n"
                    "            size_t orig_target = orig_ip + %d + (size_t)old_off;\n"
                    "            if (orig_target < code_len) {\n"
                    "                size_t new_target = offmap[orig_target];\n"
                    "                int16_t new_off = (int16_t)((long)new_target - (long)(ip + %d));\n"
                    "                write_u16(&tmp[ip + %d], (uint16_t)new_off);\n"
                    "            }\n"
                    "        }\n",
                    upper, instr->name, op_offset,
                    instr_size, instr_size, op_offset);
            }
        }
    }

    if (!has_jumps) {
        vmgen_buf_append(&src, "        (void)op;\n");
    }

    vmgen_buf_appendf(&src,
        "        if (op < %s_OP_COUNT)\n"
        "            ip += (size_t)%s_instruction_sizes[op];\n"
        "        else\n"
        "            ip++;\n"
        "    }\n\n"
        "    memcpy(code, tmp, new_len);\n"
        "    free(tmp);\n"
        "    free(offmap);\n"
        "    return new_len;\n"
        "}\n",
        upper, spec->name);

    snprintf(path, sizeof(path), "%s/optimizer.c", output_dir);
    rc = vmgen_buf_write_file(&src, path);
    vmgen_buf_free(&src);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
