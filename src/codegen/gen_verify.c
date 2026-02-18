#include "gen_verify.h"
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

int vmgen_gen_verify(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- verify.h --- */
    vmgen_buf_t hdr;
    vmgen_buf_init(&hdr);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_VERIFY_H", upper);
    vmgen_emit_header(&hdr, "verify.h", spec);
    vmgen_emit_guard_open(&hdr, guard);

    vmgen_buf_appendf(&hdr,
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "/* A single verification error */\n"
        "typedef struct {\n"
        "    size_t offset;\n"
        "    const char *message;\n"
        "} %s_verify_error_t;\n\n"
        "/* Verification result: list of errors */\n"
        "typedef struct {\n"
        "    %s_verify_error_t *errors;\n"
        "    int count;\n"
        "    int capacity;\n"
        "} %s_verify_result_t;\n\n"
        "void %s_verify_init(%s_verify_result_t *r);\n"
        "void %s_verify_free(%s_verify_result_t *r);\n\n"
        "/* Verify bytecode. Returns 0 if valid, error count otherwise. */\n"
        "int %s_verify(const uint8_t *code, size_t code_len,\n"
        "              int const_count, %s_verify_result_t *result);\n",
        spec->name,
        spec->name,
        spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    vmgen_emit_guard_close(&hdr, guard);

    char path[512];
    snprintf(path, sizeof(path), "%s/verify.h", output_dir);
    int rc = vmgen_buf_write_file(&hdr, path);
    vmgen_buf_free(&hdr);
    if (rc != 0) return rc;

    /* --- verify.c --- */
    vmgen_buf_t src;
    vmgen_buf_init(&src);
    vmgen_emit_header(&src, "verify.c", spec);

    vmgen_buf_appendf(&src,
        "#include \"verify.h\"\n"
        "#include \"opcodes.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n\n");

    /* Helper to add an error */
    vmgen_buf_appendf(&src,
        "static void add_error(%s_verify_result_t *r, size_t offset, const char *msg) {\n"
        "    if (r->count >= r->capacity) {\n"
        "        int new_cap = r->capacity ? r->capacity * 2 : 16;\n"
        "        %s_verify_error_t *newbuf = (%s_verify_error_t *)realloc(\n"
        "            r->errors, (size_t)new_cap * sizeof(*newbuf));\n"
        "        if (!newbuf) return;\n"
        "        r->errors = newbuf;\n"
        "        r->capacity = new_cap;\n"
        "    }\n"
        "    r->errors[r->count].offset = offset;\n"
        "    r->errors[r->count].message = msg;\n"
        "    r->count++;\n"
        "}\n\n",
        spec->name, spec->name, spec->name);

    /* Init/Free */
    vmgen_buf_appendf(&src,
        "void %s_verify_init(%s_verify_result_t *r) {\n"
        "    memset(r, 0, sizeof(*r));\n"
        "}\n\n"
        "void %s_verify_free(%s_verify_result_t *r) {\n"
        "    free(r->errors);\n"
        "    memset(r, 0, sizeof(*r));\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name);

    /* Main verify function */
    vmgen_buf_appendf(&src,
        "int %s_verify(const uint8_t *code, size_t code_len,\n"
        "              int const_count, %s_verify_result_t *result) {\n"
        "    size_t ip = 0;\n"
        "    while (ip < code_len) {\n"
        "        size_t instr_start = ip;\n"
        "        uint8_t opcode = code[ip++];\n\n"
        "        /* Check opcode is valid */\n"
        "        if (opcode >= %s_OP_COUNT) {\n"
        "            add_error(result, instr_start, \"invalid opcode\");\n"
        "            return result->count;\n"
        "        }\n\n"
        "        /* Check instruction fits in code */\n"
        "        int instr_size = %s_instruction_sizes[opcode];\n"
        "        if (instr_start + (size_t)instr_size > code_len) {\n"
        "            add_error(result, instr_start, \"instruction extends past end of code\");\n"
        "            return result->count;\n"
        "        }\n\n",
        spec->name, spec->name, upper, spec->name);

    /* Per-opcode validation: check operand values */
    vmgen_buf_append(&src, "        /* Per-opcode operand validation */\n");
    vmgen_buf_append(&src, "        switch (opcode) {\n");

    for (int i = 0; i < spec->instruction_count; i++) {
        const vmgen_instr_t *instr = &spec->instructions[i];
        vmgen_buf_appendf(&src, "        case %s_OP_%s: {\n", upper, instr->name);

        /* Decode operands and validate */
        int offset = 1; /* past opcode byte */
        for (int j = 0; j < instr->operand_count; j++) {
            const vmgen_operand_t *op = &instr->operands[j];

            if (op->type == VMGEN_OPERAND_UINT8) {
                vmgen_buf_appendf(&src,
                    "            uint8_t %s = code[instr_start + %d];\n",
                    op->name, offset);
                offset += 1;
            } else if (op->type == VMGEN_OPERAND_UINT16) {
                vmgen_buf_appendf(&src,
                    "            uint16_t %s = (uint16_t)(code[instr_start + %d] |\n"
                    "                          (code[instr_start + %d] << 8));\n",
                    op->name, offset, offset + 1);
                offset += 2;
            } else if (op->type == VMGEN_OPERAND_INT16) {
                vmgen_buf_appendf(&src,
                    "            int16_t %s = (int16_t)(code[instr_start + %d] |\n"
                    "                         (code[instr_start + %d] << 8));\n",
                    op->name, offset, offset + 1);
                offset += 2;
            }

            /* Validate PUSH_CONST index against constant pool */
            if (strcmp(instr->name, "PUSH_CONST") == 0 &&
                strcmp(op->name, "index") == 0) {
                vmgen_buf_appendf(&src,
                    "            if (%s >= const_count) {\n"
                    "                add_error(result, instr_start, "
                    "\"PUSH_CONST index out of range\");\n"
                    "            }\n",
                    op->name);
            }

            /* Validate jump targets */
            if (strcmp(op->name, "offset") == 0 &&
                (op->type == VMGEN_OPERAND_INT16 || op->type == VMGEN_OPERAND_UINT16)) {
                vmgen_buf_appendf(&src,
                    "            {\n"
                    "                long target = (long)instr_start + %d + (long)%s;\n"
                    "                if (target < 0 || (size_t)target > code_len) {\n"
                    "                    add_error(result, instr_start, "
                    "\"jump target out of range\");\n"
                    "                }\n"
                    "            }\n",
                    offset, op->name);
            }
        }

        /* Suppress unused variable warnings for ops with no validation */
        for (int j = 0; j < instr->operand_count; j++) {
            vmgen_buf_appendf(&src, "            (void)%s;\n",
                              instr->operands[j].name);
        }

        vmgen_buf_append(&src, "            break;\n        }\n");
    }

    vmgen_buf_appendf(&src,
        "        default:\n"
        "            break;\n"
        "        }\n\n"
        "        ip = instr_start + (size_t)instr_size;\n"
        "    }\n"
        "    return result->count;\n"
        "}\n");

    snprintf(path, sizeof(path), "%s/verify.c", output_dir);
    rc = vmgen_buf_write_file(&src, path);
    vmgen_buf_free(&src);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
