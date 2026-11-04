#include "gen_bytecode.h"
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

int vmgen_gen_bytecode(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- bytecode.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/bytecode.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_BYTECODE_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "bytecode.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n"
        "#include \"vm.h\"\n\n");

    /* Magic and version */
    vmgen_buf_appendf(&buf,
        "#define %s_BC_MAGIC   0x564D4743  /* \"VMGC\" */\n"
        "#define %s_BC_VERSION 1\n\n",
        upper, upper);

    /* Constant type tags */
    vmgen_buf_appendf(&buf,
        "typedef enum {\n"
        "    %s_BC_CONST_INT64  = 1,\n"
        "    %s_BC_CONST_DOUBLE = 2,\n"
        "    %s_BC_CONST_STRING = 3,\n"
        "} %s_bc_const_type_t;\n\n",
        upper, upper, upper, spec->name);

    /* File header struct */
    vmgen_buf_appendf(&buf,
        "typedef struct {\n"
        "    uint32_t magic;\n"
        "    uint32_t version;\n"
        "    uint32_t const_count;\n"
        "    uint32_t code_size;\n"
        "} %s_bc_header_t;\n\n",
        spec->name);

    /* API */
    vmgen_buf_appendf(&buf,
        "int %s_bc_save(const char *path, const %s_vm_t *vm,\n"
        "               const uint8_t *code, size_t code_len);\n"
        "int %s_bc_load(const char *path, %s_vm_t *vm,\n"
        "               uint8_t **code_out, size_t *code_len_out);\n"
        "void %s_bc_free_code(uint8_t *code);\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- bytecode.c --- */
    snprintf(path, sizeof(path), "%s/bytecode.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "bytecode.c", spec);

    vmgen_buf_append(&buf,
        "#include \"bytecode.h\"\n"
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n\n");

    /* write helpers */
    vmgen_buf_append(&buf,
        "static int write_u32(FILE *f, uint32_t v) {\n"
        "    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8),\n"
        "                     (uint8_t)(v>>16), (uint8_t)(v>>24) };\n"
        "    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;\n"
        "}\n\n"
        "static int read_u32(FILE *f, uint32_t *v) {\n"
        "    uint8_t b[4];\n"
        "    if (fread(b, 1, 4, f) != 4) return -1;\n"
        "    *v = (uint32_t)b[0] | ((uint32_t)b[1]<<8) |\n"
        "         ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);\n"
        "    return 0;\n"
        "}\n\n");

    /* save */
    vmgen_buf_appendf(&buf,
        "int %s_bc_save(const char *path, const %s_vm_t *vm,\n"
        "               const uint8_t *code, size_t code_len) {\n"
        "    FILE *f = fopen(path, \"wb\");\n"
        "    if (!f) { fprintf(stderr, \"bc: cannot open '%%s' for writing\\n\", path); return -1; }\n\n"
        "    /* Header */\n"
        "    if (write_u32(f, %s_BC_MAGIC) != 0) goto fail;\n"
        "    if (write_u32(f, %s_BC_VERSION) != 0) goto fail;\n"
        "    if (write_u32(f, (uint32_t)vm->constants.count) != 0) goto fail;\n"
        "    if (write_u32(f, (uint32_t)code_len) != 0) goto fail;\n\n",
        spec->name, spec->name,
        upper, upper);

    /* Write constants — depends on value type */
    vmgen_buf_append(&buf, "    /* Constants */\n");
    vmgen_buf_append(&buf, "    for (int i = 0; i < vm->constants.count; i++) {\n");

    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        vmgen_buf_appendf(&buf,
            "        switch (vm->constants.values[i].type) {\n"
            "        case %s_VAL_INT64: {\n"
            "            uint8_t tag = %s_BC_CONST_INT64;\n"
            "            fwrite(&tag, 1, 1, f);\n"
            "            int64_t v = vm->constants.values[i].as.i64;\n"
            "            fwrite(&v, sizeof(v), 1, f);\n"
            "            break;\n"
            "        }\n"
            "        case %s_VAL_DOUBLE: {\n"
            "            uint8_t tag = %s_BC_CONST_DOUBLE;\n"
            "            fwrite(&tag, 1, 1, f);\n"
            "            double v = vm->constants.values[i].as.f64;\n"
            "            fwrite(&v, sizeof(v), 1, f);\n"
            "            break;\n"
            "        }\n",
            upper, upper,
            upper, upper);
        /* User-defined types carry embedder-specific meaning (often a
         * pointer) and cannot be serialized */
        for (int ci = 0; ci < spec->custom_type_count; ci++) {
            char tu[VMGEN_MAX_NAME];
            make_upper(tu, spec->custom_types[ci].name, sizeof(tu));
            vmgen_buf_appendf(&buf, "        case %s_VAL_%s:\n", upper, tu);
        }
        if (spec->custom_type_count > 0) {
            vmgen_buf_append(&buf,
                "            fprintf(stderr, \"bytecode save: constant %d has a custom "
                "type that cannot be serialized\\n\", i);\n"
                "            goto fail;\n");
        }
        vmgen_buf_appendf(&buf,
            "        default: {\n"
            "            uint8_t tag = %s_BC_CONST_INT64;\n"
            "            fwrite(&tag, 1, 1, f);\n"
            "            int64_t v = vm->constants.values[i].as.i64;\n"
            "            fwrite(&v, sizeof(v), 1, f);\n"
            "            break;\n"
            "        }\n"
            "        }\n",
            upper);
    } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
        vmgen_buf_appendf(&buf,
            "        uint8_t tag = %s_BC_CONST_DOUBLE;\n"
            "        fwrite(&tag, 1, 1, f);\n"
            "        double v = vm->constants.values[i];\n"
            "        fwrite(&v, sizeof(v), 1, f);\n",
            upper);
    } else {
        vmgen_buf_appendf(&buf,
            "        uint8_t tag = %s_BC_CONST_INT64;\n"
            "        fwrite(&tag, 1, 1, f);\n"
            "        int64_t v = vm->constants.values[i];\n"
            "        fwrite(&v, sizeof(v), 1, f);\n",
            upper);
    }

    vmgen_buf_append(&buf, "    }\n\n");

    /* Write code section */
    vmgen_buf_append(&buf,
        "    /* Code */\n"
        "    if (fwrite(code, 1, code_len, f) != code_len) goto fail;\n\n"
        "    fclose(f);\n"
        "    return 0;\n"
        "fail:\n"
        "    fclose(f);\n"
        "    return -1;\n"
        "}\n\n");

    /* load */
    vmgen_buf_appendf(&buf,
        "int %s_bc_load(const char *path, %s_vm_t *vm,\n"
        "               uint8_t **code_out, size_t *code_len_out) {\n"
        "    FILE *f = fopen(path, \"rb\");\n"
        "    if (!f) { fprintf(stderr, \"bc: cannot open '%%s' for reading\\n\", path); return -1; }\n\n"
        "    /* Header */\n"
        "    uint32_t magic, version, const_count, code_size;\n"
        "    if (read_u32(f, &magic) != 0 || read_u32(f, &version) != 0 ||\n"
        "        read_u32(f, &const_count) != 0 || read_u32(f, &code_size) != 0) {\n"
        "        fprintf(stderr, \"bc: truncated header\\n\");\n"
        "        goto fail;\n"
        "    }\n"
        "    if (magic != %s_BC_MAGIC) {\n"
        "        fprintf(stderr, \"bc: bad magic 0x%%08x\\n\", magic);\n"
        "        goto fail;\n"
        "    }\n"
        "    if (version != %s_BC_VERSION) {\n"
        "        fprintf(stderr, \"bc: unsupported version %%u\\n\", version);\n"
        "        goto fail;\n"
        "    }\n\n",
        spec->name, spec->name,
        upper, upper);

    /* Read constants */
    vmgen_buf_append(&buf, "    /* Constants */\n");
    vmgen_buf_append(&buf, "    for (uint32_t i = 0; i < const_count; i++) {\n");
    vmgen_buf_append(&buf,
        "        uint8_t tag;\n"
        "        if (fread(&tag, 1, 1, f) != 1) goto fail;\n");

    if (spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION) {
        vmgen_buf_appendf(&buf,
            "        switch (tag) {\n"
            "        case %s_BC_CONST_INT64: {\n"
            "            int64_t v;\n"
            "            if (fread(&v, sizeof(v), 1, f) != 1) goto fail;\n"
            "            %s_vm_add_constant(vm, %s_INT64(v));\n"
            "            break;\n"
            "        }\n"
            "        case %s_BC_CONST_DOUBLE: {\n"
            "            double v;\n"
            "            if (fread(&v, sizeof(v), 1, f) != 1) goto fail;\n"
            "            %s_vm_add_constant(vm, %s_DOUBLE(v));\n"
            "            break;\n"
            "        }\n"
            "        default:\n"
            "            fprintf(stderr, \"bc: unknown constant type %%d\\n\", tag);\n"
            "            goto fail;\n"
            "        }\n",
            upper, spec->name, upper,
            upper, spec->name, upper);
    } else if (spec->stack.value_type == VMGEN_VALUE_DOUBLE) {
        vmgen_buf_appendf(&buf,
            "        if (tag != %s_BC_CONST_DOUBLE) goto fail;\n"
            "        double v;\n"
            "        if (fread(&v, sizeof(v), 1, f) != 1) goto fail;\n"
            "        %s_vm_add_constant(vm, v);\n",
            upper, spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "        if (tag != %s_BC_CONST_INT64) goto fail;\n"
            "        int64_t v;\n"
            "        if (fread(&v, sizeof(v), 1, f) != 1) goto fail;\n"
            "        %s_vm_add_constant(vm, v);\n",
            upper, spec->name);
    }

    vmgen_buf_append(&buf, "    }\n\n");

    /* Read code */
    vmgen_buf_append(&buf,
        "    /* Code */\n"
        "    uint8_t *code = (uint8_t *)malloc(code_size);\n"
        "    if (!code) { fprintf(stderr, \"bc: out of memory\\n\"); goto fail; }\n"
        "    if (fread(code, 1, code_size, f) != code_size) {\n"
        "        free(code);\n"
        "        goto fail;\n"
        "    }\n\n"
        "    *code_out = code;\n"
        "    *code_len_out = code_size;\n"
        "    fclose(f);\n"
        "    return 0;\n"
        "fail:\n"
        "    fclose(f);\n"
        "    return -1;\n"
        "}\n\n");

    /* free_code */
    vmgen_buf_appendf(&buf,
        "void %s_bc_free_code(uint8_t *code) {\n"
        "    free(code);\n"
        "}\n",
        spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
