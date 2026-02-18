#include "gen_disasm.h"
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

static const char *printf_fmt_for(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  return "%u";
    case VMGEN_OPERAND_UINT16: return "%u";
    case VMGEN_OPERAND_UINT32: return "%u";
    case VMGEN_OPERAND_INT8:   return "%d";
    case VMGEN_OPERAND_INT16:  return "%d";
    case VMGEN_OPERAND_INT32:  return "%d";
    }
    return "%u";
}

static const char *c_read_expr(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:
        return "code[offset++]";
    case VMGEN_OPERAND_INT8:
        return "(int8_t)code[offset++]";
    case VMGEN_OPERAND_UINT16:
        return "(offset += 2, (uint16_t)(code[offset-2] | (code[offset-1] << 8)))";
    case VMGEN_OPERAND_INT16:
        return "(int16_t)(offset += 2, (uint16_t)(code[offset-2] | (code[offset-1] << 8)))";
    case VMGEN_OPERAND_UINT32:
        return "(offset += 4, (uint32_t)(code[offset-4] | (code[offset-3] << 8) | (code[offset-2] << 16) | (code[offset-1] << 24)))";
    case VMGEN_OPERAND_INT32:
        return "(int32_t)(offset += 4, (uint32_t)(code[offset-4] | (code[offset-3] << 8) | (code[offset-2] << 16) | (code[offset-1] << 24)))";
    }
    return "code[offset++]";
}

int vmgen_gen_disasm(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- disasm.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/disasm.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_DISASM_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "disasm.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf, "#include <stdint.h>\n");
    vmgen_buf_append(&buf, "#include <stddef.h>\n\n");

    vmgen_buf_appendf(&buf,
        "/* Disassemble a single instruction at offset.\n"
        " * Returns the number of bytes consumed, or -1 on error. */\n"
        "int %s_disasm_instruction(const uint8_t *code, size_t code_len, size_t offset);\n\n"
        "/* Disassemble an entire bytecode buffer (prints to stdout). */\n"
        "void %s_disasm(const uint8_t *code, size_t code_len);\n\n"
        "/* Disassemble a single instruction into a buffer.\n"
        " * Returns bytes consumed from code, or -1 on error.\n"
        " * Writes at most buf_size-1 chars + NUL to buf. */\n"
        "int %s_disasm_instruction_buf(const uint8_t *code, size_t code_len,\n"
        "    size_t offset, char *buf, size_t buf_size);\n\n"
        "/* Disassemble an entire bytecode buffer into a string.\n"
        " * Returns number of chars written (excluding NUL), or -1 on error. */\n"
        "int %s_disasm_buf(const uint8_t *code, size_t code_len,\n"
        "    char *buf, size_t buf_size);\n",
        spec->name, spec->name, spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- disasm.c --- */
    snprintf(path, sizeof(path), "%s/disasm.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "disasm.c", spec);

    vmgen_buf_append(&buf,
        "#include \"disasm.h\"\n"
        "#include \"opcodes.h\"\n"
        "#include <stdio.h>\n\n");

    /* disasm_instruction */
    vmgen_buf_appendf(&buf,
        "int %s_disasm_instruction(const uint8_t *code, size_t code_len, size_t offset) {\n"
        "    if (offset >= code_len) return -1;\n"
        "    size_t start = offset;\n"
        "    uint8_t opcode = code[offset++];\n\n"
        "    if (opcode >= %s_OP_COUNT) {\n"
        "        printf(\"%%04zu  UNKNOWN(%%d)\\n\", start, opcode);\n"
        "        return -1;\n"
        "    }\n\n"
        "    printf(\"%%04zu  %%-16s\", start, %s_opcode_names[opcode]);\n\n"
        "    switch (opcode) {\n",
        spec->name, upper, spec->name);

    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(&buf, "    case %s_OP_%s:\n", upper, instr->name);

        if (instr->operand_count == 0) {
            vmgen_buf_append(&buf, "        printf(\"\\n\");\n");
        } else {
            vmgen_buf_append(&buf, "        printf(\"");
            for (int j = 0; j < instr->operand_count; j++) {
                if (j > 0) vmgen_buf_append(&buf, " ");
                vmgen_buf_appendf(&buf, "%s", printf_fmt_for(instr->operands[j].type));
            }
            vmgen_buf_append(&buf, "\\n\"");
            for (int j = 0; j < instr->operand_count; j++) {
                vmgen_buf_appendf(&buf, ", %s", c_read_expr(instr->operands[j].type));
            }
            vmgen_buf_append(&buf, ");\n");
        }

        vmgen_buf_append(&buf, "        break;\n");
    }

    vmgen_buf_appendf(&buf,
        "    default:\n"
        "        printf(\"\\n\");\n"
        "        break;\n"
        "    }\n"
        "    return (int)(offset - start);\n"
        "}\n\n");

    /* disasm - full buffer (stdout) */
    vmgen_buf_appendf(&buf,
        "void %s_disasm(const uint8_t *code, size_t code_len) {\n"
        "    size_t offset = 0;\n"
        "    while (offset < code_len) {\n"
        "        int consumed = %s_disasm_instruction(code, code_len, offset);\n"
        "        if (consumed <= 0) break;\n"
        "        offset += (size_t)consumed;\n"
        "    }\n"
        "}\n\n",
        spec->name, spec->name);

    /* disasm_instruction_buf — snprintf version */
    vmgen_buf_appendf(&buf,
        "int %s_disasm_instruction_buf(const uint8_t *code, size_t code_len,\n"
        "    size_t offset, char *buf, size_t buf_size) {\n"
        "    if (offset >= code_len || !buf || buf_size == 0) return -1;\n"
        "    size_t start = offset;\n"
        "    uint8_t opcode = code[offset++];\n"
        "    int written = 0;\n"
        "    int n;\n\n"
        "    if (opcode >= %s_OP_COUNT) {\n"
        "        snprintf(buf, buf_size, \"%%04zu  UNKNOWN(%%d)\\n\", start, opcode);\n"
        "        return -1;\n"
        "    }\n\n"
        "    n = snprintf(buf + written, buf_size - (size_t)written,\n"
        "        \"%%04zu  %%-16s\", start, %s_opcode_names[opcode]);\n"
        "    if (n > 0) written += n;\n\n"
        "    switch (opcode) {\n",
        spec->name, upper, spec->name);

    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(&buf, "    case %s_OP_%s:\n", upper, instr->name);

        if (instr->operand_count == 0) {
            vmgen_buf_append(&buf,
                "        n = snprintf(buf + written, buf_size - (size_t)written, \"\\n\");\n"
                "        if (n > 0) written += n;\n");
        } else {
            vmgen_buf_append(&buf,
                "        n = snprintf(buf + written, buf_size - (size_t)written, \"");
            for (int j = 0; j < instr->operand_count; j++) {
                if (j > 0) vmgen_buf_append(&buf, " ");
                vmgen_buf_appendf(&buf, "%s", printf_fmt_for(instr->operands[j].type));
            }
            vmgen_buf_append(&buf, "\\n\"");
            for (int j = 0; j < instr->operand_count; j++) {
                vmgen_buf_appendf(&buf, ", %s", c_read_expr(instr->operands[j].type));
            }
            vmgen_buf_append(&buf, ");\n        if (n > 0) written += n;\n");
        }

        vmgen_buf_append(&buf, "        break;\n");
    }

    vmgen_buf_appendf(&buf,
        "    default:\n"
        "        n = snprintf(buf + written, buf_size - (size_t)written, \"\\n\");\n"
        "        if (n > 0) written += n;\n"
        "        break;\n"
        "    }\n"
        "    (void)written;\n"
        "    return (int)(offset - start);\n"
        "}\n\n");

    /* disasm_buf — full buffer to string */
    vmgen_buf_appendf(&buf,
        "int %s_disasm_buf(const uint8_t *code, size_t code_len,\n"
        "    char *buf, size_t buf_size) {\n"
        "    if (!buf || buf_size == 0) return -1;\n"
        "    buf[0] = '\\0';\n"
        "    int total = 0;\n"
        "    size_t offset = 0;\n"
        "    while (offset < code_len) {\n"
        "        char line[256];\n"
        "        int consumed = %s_disasm_instruction_buf(code, code_len, offset, line, sizeof(line));\n"
        "        if (consumed <= 0) break;\n"
        "        offset += (size_t)consumed;\n"
        "        size_t remaining = buf_size - (size_t)total;\n"
        "        if (remaining > 1) {\n"
        "            int n = snprintf(buf + total, remaining, \"%%s\", line);\n"
        "            if (n > 0) total += n;\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n",
        spec->name, spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
