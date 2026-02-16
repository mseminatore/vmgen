#include "gen_native.h"
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

int vmgen_gen_native(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- native.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/native.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_NATIVE_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "native.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stddef.h>\n\n");

    /* Forward-declare the VM type so native.h is self-contained */
    vmgen_buf_appendf(&buf,
        "typedef struct %s_vm %s_vm_t;\n\n",
        spec->name, spec->name);

    /* Native function typedef */
    vmgen_buf_appendf(&buf,
        "typedef int (*%s_native_fn_t)(%s_vm_t *vm, int arg_count);\n\n",
        spec->name, spec->name);

    /* Native function entry */
    vmgen_buf_appendf(&buf,
        "typedef struct {\n"
        "    const char *name;\n"
        "    %s_native_fn_t fn;\n"
        "    int arity;\n"
        "} %s_native_entry_t;\n\n",
        spec->name, spec->name);

    /* Native function table */
    vmgen_buf_appendf(&buf,
        "typedef struct {\n"
        "    %s_native_entry_t *entries;\n"
        "    int count;\n"
        "    int capacity;\n"
        "} %s_native_table_t;\n\n",
        spec->name, spec->name);

    /* API */
    vmgen_buf_appendf(&buf,
        "void %s_native_table_init(%s_native_table_t *table);\n"
        "void %s_native_table_free(%s_native_table_t *table);\n"
        "int  %s_native_register(%s_native_table_t *table, const char *name,\n"
        "                        %s_native_fn_t fn, int arity);\n"
        "int  %s_native_find(const %s_native_table_t *table, const char *name);\n"
        "int  %s_native_call(%s_native_table_t *table, int index,\n"
        "                    %s_vm_t *vm, int arg_count);\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- native.c --- */
    snprintf(path, sizeof(path), "%s/native.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "native.c", spec);

    vmgen_buf_append(&buf,
        "#include \"native.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n\n");

    /* init */
    vmgen_buf_appendf(&buf,
        "void %s_native_table_init(%s_native_table_t *table) {\n"
        "    table->entries = NULL;\n"
        "    table->count = 0;\n"
        "    table->capacity = 0;\n"
        "}\n\n",
        spec->name, spec->name);

    /* free */
    vmgen_buf_appendf(&buf,
        "void %s_native_table_free(%s_native_table_t *table) {\n"
        "    free(table->entries);\n"
        "    table->entries = NULL;\n"
        "    table->count = 0;\n"
        "    table->capacity = 0;\n"
        "}\n\n",
        spec->name, spec->name);

    /* register */
    vmgen_buf_appendf(&buf,
        "int %s_native_register(%s_native_table_t *table, const char *name,\n"
        "                       %s_native_fn_t fn, int arity) {\n"
        "    if (table->count >= table->capacity) {\n"
        "        int new_cap = table->capacity < 8 ? 8 : table->capacity * 2;\n"
        "        %s_native_entry_t *new_entries = (%s_native_entry_t *)realloc(\n"
        "            table->entries, (size_t)new_cap * sizeof(%s_native_entry_t));\n"
        "        if (!new_entries) {\n"
        "            fprintf(stderr, \"native_register: out of memory\\n\");\n"
        "            return -1;\n"
        "        }\n"
        "        table->entries = new_entries;\n"
        "        table->capacity = new_cap;\n"
        "    }\n"
        "    table->entries[table->count].name = name;\n"
        "    table->entries[table->count].fn = fn;\n"
        "    table->entries[table->count].arity = arity;\n"
        "    return table->count++;\n"
        "}\n\n",
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name);

    /* find */
    vmgen_buf_appendf(&buf,
        "int %s_native_find(const %s_native_table_t *table, const char *name) {\n"
        "    for (int i = 0; i < table->count; i++) {\n"
        "        if (strcmp(table->entries[i].name, name) == 0)\n"
        "            return i;\n"
        "    }\n"
        "    return -1;\n"
        "}\n\n",
        spec->name, spec->name);

    /* call */
    vmgen_buf_appendf(&buf,
        "int %s_native_call(%s_native_table_t *table, int index,\n"
        "                   %s_vm_t *vm, int arg_count) {\n"
        "    if (index < 0 || index >= table->count) {\n"
        "        fprintf(stderr, \"native_call: invalid index %%d\\n\", index);\n"
        "        return -1;\n"
        "    }\n"
        "    return table->entries[index].fn(vm, arg_count);\n"
        "}\n",
        spec->name, spec->name, spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
