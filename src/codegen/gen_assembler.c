#include "gen_assembler.h"
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

static int operand_size(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  case VMGEN_OPERAND_INT8:  return 1;
    case VMGEN_OPERAND_UINT16: case VMGEN_OPERAND_INT16: return 2;
    case VMGEN_OPERAND_UINT32: case VMGEN_OPERAND_INT32: return 4;
    }
    return 1;
}

static const char *scanf_fmt_for(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  case VMGEN_OPERAND_UINT16:
    case VMGEN_OPERAND_UINT32: return "%u";
    case VMGEN_OPERAND_INT8:   case VMGEN_OPERAND_INT16:
    case VMGEN_OPERAND_INT32:  return "%d";
    }
    return "%u";
}

/* C type matching scanf_fmt_for() — %u needs unsigned int*, %d needs int* */
static const char *scanf_type_for(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_INT8:   case VMGEN_OPERAND_INT16:
    case VMGEN_OPERAND_INT32:  return "int";
    default:                   return "unsigned int";
    }
}

int vmgen_gen_assembler(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- assembler.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/assembler.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_ASSEMBLER_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "assembler.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n\n");

    vmgen_buf_appendf(&buf,
        "#define %s_ASM_MAX_CODE (64 * 1024)\n\n"
        "typedef struct {\n"
        "    uint8_t code[%s_ASM_MAX_CODE];\n"
        "    size_t len;\n"
        "    int error;\n"
        "} %s_assembler_t;\n\n"
        "void %s_asm_init(%s_assembler_t *as);\n"
        "void %s_asm_emit_byte(%s_assembler_t *as, uint8_t byte);\n"
        "void %s_asm_emit_u16(%s_assembler_t *as, uint16_t val);\n"
        "void %s_asm_emit_u32(%s_assembler_t *as, uint32_t val);\n"
        "int  %s_asm_assemble_line(%s_assembler_t *as, const char *line);\n"
        "int  %s_asm_assemble(%s_assembler_t *as, const char *source);\n",
        upper, upper, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- assembler.c --- */
    snprintf(path, sizeof(path), "%s/assembler.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "assembler.c", spec);

    vmgen_buf_append(&buf,
        "#include \"assembler.h\"\n"
        "#include \"opcodes.h\"\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <ctype.h>\n\n"
        "#ifdef _MSC_VER\n"
        "#pragma warning(disable: 4996) /* sscanf */\n"
        "#endif\n\n");

    /* asm_init */
    vmgen_buf_appendf(&buf,
        "void %s_asm_init(%s_assembler_t *as) {\n"
        "    memset(as, 0, sizeof(*as));\n"
        "}\n\n",
        spec->name, spec->name);

    /* emit_byte */
    vmgen_buf_appendf(&buf,
        "void %s_asm_emit_byte(%s_assembler_t *as, uint8_t byte) {\n"
        "    if (as->len >= %s_ASM_MAX_CODE) {\n"
        "        fprintf(stderr, \"assembler: code buffer overflow\\n\");\n"
        "        as->error = 1;\n"
        "        return;\n"
        "    }\n"
        "    as->code[as->len++] = byte;\n"
        "}\n\n",
        spec->name, spec->name, upper);

    /* emit_u16 (little-endian) */
    vmgen_buf_appendf(&buf,
        "void %s_asm_emit_u16(%s_assembler_t *as, uint16_t val) {\n"
        "    %s_asm_emit_byte(as, (uint8_t)(val & 0xFF));\n"
        "    %s_asm_emit_byte(as, (uint8_t)((val >> 8) & 0xFF));\n"
        "}\n\n",
        spec->name, spec->name, spec->name, spec->name);

    /* emit_u32 (little-endian) */
    vmgen_buf_appendf(&buf,
        "void %s_asm_emit_u32(%s_assembler_t *as, uint32_t val) {\n"
        "    %s_asm_emit_byte(as, (uint8_t)(val & 0xFF));\n"
        "    %s_asm_emit_byte(as, (uint8_t)((val >> 8) & 0xFF));\n"
        "    %s_asm_emit_byte(as, (uint8_t)((val >> 16) & 0xFF));\n"
        "    %s_asm_emit_byte(as, (uint8_t)((val >> 24) & 0xFF));\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    /* assemble_line: parse mnemonic + operands */
    vmgen_buf_appendf(&buf,
        "int %s_asm_assemble_line(%s_assembler_t *as, const char *line) {\n"
        "    char mnemonic[64];\n"
        "    int pos = 0;\n\n"
        "    /* Skip whitespace */\n"
        "    while (*line && isspace((unsigned char)*line)) line++;\n"
        "    /* Skip empty lines and comments */\n"
        "    if (*line == '\\0' || *line == ';' || *line == '#') return 0;\n\n"
        "    /* Read mnemonic */\n"
        "    while (*line && !isspace((unsigned char)*line) && pos < 63)\n"
        "        mnemonic[pos++] = *line++;\n"
        "    mnemonic[pos] = '\\0';\n\n",
        spec->name, spec->name);

    /* Match mnemonic to opcode */
    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        const char *elif = (i == 0) ? "if" : "} else if";

        vmgen_buf_appendf(&buf,
            "    %s (strcmp(mnemonic, \"%s\") == 0) {\n"
            "        %s_asm_emit_byte(as, %s_OP_%s);\n",
            elif, instr->name, spec->name, upper, instr->name);

        /* Parse operands */
        for (int j = 0; j < instr->operand_count; j++) {
            int sz = operand_size(instr->operands[j].type);
            vmgen_buf_appendf(&buf,
                "        { %s val = 0; sscanf(line, \" %s\", &val); ",
                scanf_type_for(instr->operands[j].type),
                scanf_fmt_for(instr->operands[j].type));

            /* Advance line past this operand */
            vmgen_buf_append(&buf,
                "while (*line && isspace((unsigned char)*line)) line++; "
                "while (*line && !isspace((unsigned char)*line)) line++; ");

            if (sz == 1) {
                vmgen_buf_appendf(&buf,
                    "%s_asm_emit_byte(as, (uint8_t)val); }\n", spec->name);
            } else if (sz == 2) {
                vmgen_buf_appendf(&buf,
                    "%s_asm_emit_u16(as, (uint16_t)val); }\n", spec->name);
            } else {
                vmgen_buf_appendf(&buf,
                    "%s_asm_emit_u32(as, (uint32_t)val); }\n", spec->name);
            }
        }
    }

    vmgen_buf_append(&buf,
        "    } else {\n"
        "        fprintf(stderr, \"assembler: unknown mnemonic '%s'\\n\", mnemonic);\n"
        "        as->error = 1;\n"
        "        return -1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n");

    /* assemble: process multi-line source */
    vmgen_buf_appendf(&buf,
        "int %s_asm_assemble(%s_assembler_t *as, const char *source) {\n"
        "    char line[256];\n"
        "    const char *p = source;\n\n"
        "    while (*p) {\n"
        "        /* Extract one line */\n"
        "        int i = 0;\n"
        "        while (*p && *p != '\\n' && i < 255)\n"
        "            line[i++] = *p++;\n"
        "        line[i] = '\\0';\n"
        "        if (*p == '\\n') p++;\n\n"
        "        if (%s_asm_assemble_line(as, line) != 0)\n"
        "            return -1;\n"
        "    }\n"
        "    return as->error ? -1 : 0;\n"
        "}\n",
        spec->name, spec->name, spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
