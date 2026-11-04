#include "gen_opcodes.h"
#include "emit.h"
#include "../util/buffer.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Generate uppercase version of VM name for use in guards/prefixes */
static void make_upper(char *dst, const char *src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* Return the byte size of an operand type */
static int operand_size(vmgen_operand_type_t type) {
    switch (type) {
    case VMGEN_OPERAND_UINT8:  case VMGEN_OPERAND_INT8:  return 1;
    case VMGEN_OPERAND_UINT16: case VMGEN_OPERAND_INT16: return 2;
    case VMGEN_OPERAND_UINT32: case VMGEN_OPERAND_INT32: return 4;
    }
    return 1;
}

int vmgen_gen_opcodes(const vmgen_spec_t *spec, const char *output_dir) {
    char upper_name[VMGEN_MAX_NAME];
    make_upper(upper_name, spec->name, sizeof(upper_name));

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_OPCODES_H", upper_name);

    char path[512];
    snprintf(path, sizeof(path), "%s/opcodes.h", output_dir);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "opcodes.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf, "#include <stdint.h>\n\n");

    /* Opcode enum */
    vmgen_buf_appendf(&buf, "typedef enum {\n");
    for (int i = 0; i < spec->instruction_count; i++) {
        vmgen_buf_appendf(&buf, "    %s_OP_%s = %d,\n",
                          upper_name, spec->instructions[i].name, i);
    }
    vmgen_buf_appendf(&buf, "    %s_OP_COUNT = %d\n",
                      upper_name, spec->instruction_count);
    vmgen_buf_appendf(&buf, "} %s_opcode_t;\n\n", spec->name);

    /* Opcode name strings */
    vmgen_buf_appendf(&buf, "static const char *const %s_opcode_names[] = {\n",
                      spec->name);
    for (int i = 0; i < spec->instruction_count; i++) {
        vmgen_buf_appendf(&buf, "    \"%s\",\n", spec->instructions[i].name);
    }
    vmgen_buf_append(&buf, "};\n\n");

    /* Operand count table */
    vmgen_buf_appendf(&buf, "static const int %s_operand_counts[] = {\n",
                      spec->name);
    for (int i = 0; i < spec->instruction_count; i++) {
        vmgen_buf_appendf(&buf, "    %d,  /* %s */\n",
                          spec->instructions[i].operand_count,
                          spec->instructions[i].name);
    }
    vmgen_buf_append(&buf, "};\n\n");

    /* Instruction size table (opcode byte + operand bytes) */
    vmgen_buf_appendf(&buf, "/* Total instruction size in bytes (1 opcode byte + operands) */\n");
    vmgen_buf_appendf(&buf, "static const int %s_instruction_sizes[] = {\n",
                      spec->name);
    for (int i = 0; i < spec->instruction_count; i++) {
        int size = 1; /* opcode byte */
        for (int j = 0; j < spec->instructions[i].operand_count; j++) {
            size += operand_size(spec->instructions[i].operands[j].type);
        }
        vmgen_buf_appendf(&buf, "    %d,  /* %s */\n",
                          size, spec->instructions[i].name);
    }
    vmgen_buf_append(&buf, "};\n");

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);

    if (rc == 0)
        printf("vmgen: generated %s\n", path);
    return rc;
}
