#include "gen_modules.h"
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

int vmgen_gen_modules(const vmgen_spec_t *spec, const char *output_dir) {
    if (!spec->features.modules_enabled)
        return 0;

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- modules.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/modules.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_MODULES_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "modules.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n\n");

    /* Module state enum */
    vmgen_buf_appendf(&buf,
        "typedef enum {\n"
        "    %s_MODULE_UNLOADED,\n"
        "    %s_MODULE_LOADING,\n"
        "    %s_MODULE_LOADED,\n"
        "    %s_MODULE_ERROR,\n"
        "} %s_module_state_t;\n\n",
        upper, upper, upper, upper, spec->name);

    /* Module struct */
    vmgen_buf_appendf(&buf,
        "#define %s_MODULE_MAX_GLOBALS 256\n\n"
        "typedef struct {\n"
        "    const char *name;\n"
        "    uint8_t *bytecode;\n"
        "    size_t bytecode_size;\n"
        "    void **constant_pool;\n"
        "    size_t constant_count;\n"
        "    %s_module_state_t state;\n"
        "    int64_t globals[%s_MODULE_MAX_GLOBALS];\n"
        "} %s_module_t;\n\n",
        upper,
        spec->name, upper,
        spec->name);

    /* Module table struct */
    vmgen_buf_appendf(&buf,
        "typedef struct {\n"
        "    %s_module_t **modules;\n"
        "    size_t count;\n"
        "    size_t capacity;\n"
        "} %s_module_table_t;\n\n",
        spec->name, spec->name);

    /* API */
    vmgen_buf_appendf(&buf,
        "void %s_module_table_init(%s_module_table_t *table);\n"
        "void %s_module_table_free(%s_module_table_t *table);\n"
        "int  %s_module_load(%s_module_table_t *table, const char *name, const uint8_t *bytecode, size_t size);\n"
        "%s_module_t *%s_module_find(%s_module_table_t *table, const char *name);\n"
        "int64_t %s_module_get_global(%s_module_t *mod, size_t index);\n"
        "void    %s_module_set_global(%s_module_t *mod, size_t index, int64_t value);\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- modules.c --- */
    snprintf(path, sizeof(path), "%s/modules.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "modules.c", spec);

    vmgen_buf_append(&buf,
        "#include \"modules.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n\n");

    /* module_table_init */
    vmgen_buf_appendf(&buf,
        "void %s_module_table_init(%s_module_table_t *table) {\n"
        "    table->modules = NULL;\n"
        "    table->count = 0;\n"
        "    table->capacity = 0;\n"
        "}\n\n",
        spec->name, spec->name);

    /* module_table_free */
    vmgen_buf_appendf(&buf,
        "void %s_module_table_free(%s_module_table_t *table) {\n"
        "    for (size_t i = 0; i < table->count; i++) {\n"
        "        free(table->modules[i]);\n"
        "    }\n"
        "    free(table->modules);\n"
        "    table->modules = NULL;\n"
        "    table->count = 0;\n"
        "    table->capacity = 0;\n"
        "}\n\n",
        spec->name, spec->name);

    /* module_load */
    vmgen_buf_appendf(&buf,
        "int %s_module_load(%s_module_table_t *table, const char *name, const uint8_t *bytecode, size_t size) {\n"
        "    if (%s_module_find(table, name) != NULL)\n"
        "        return -1; /* already loaded */\n\n"
        "    if (table->count == table->capacity) {\n"
        "        size_t new_cap = table->capacity ? table->capacity * 2 : 8;\n"
        "        %s_module_t **tmp = (%s_module_t **)realloc(table->modules,\n"
        "            new_cap * sizeof(%s_module_t *));\n"
        "        if (!tmp) return -1;\n"
        "        table->modules = tmp;\n"
        "        table->capacity = new_cap;\n"
        "    }\n\n"
        "    %s_module_t *mod = (%s_module_t *)calloc(1, sizeof(%s_module_t));\n"
        "    if (!mod) return -1;\n"
        "    mod->name = name;\n"
        "    mod->bytecode = (uint8_t *)bytecode;\n"
        "    mod->bytecode_size = size;\n"
        "    mod->constant_pool = NULL;\n"
        "    mod->constant_count = 0;\n",
        spec->name, spec->name,
        spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name);

    /* Emit loading-mode-specific comment */
    if (spec->features.module_loading == VMGEN_MODULE_STATIC) {
        vmgen_buf_append(&buf,
            "    /* Static loading: modules are compiled in at build time */\n");
    } else if (spec->features.module_loading == VMGEN_MODULE_DYNAMIC) {
        vmgen_buf_append(&buf,
            "    /* Dynamic loading: modules are loaded at runtime from bytecode */\n");
    } else {
        vmgen_buf_append(&buf,
            "    /* Both static and dynamic loading supported */\n");
    }

    vmgen_buf_appendf(&buf,
        "    mod->state = %s_MODULE_LOADED;\n"
        "    table->modules[table->count++] = mod;\n"
        "    return 0;\n"
        "}\n\n",
        upper);

    /* module_find */
    vmgen_buf_appendf(&buf,
        "%s_module_t *%s_module_find(%s_module_table_t *table, const char *name) {\n"
        "    for (size_t i = 0; i < table->count; i++) {\n"
        "        if (strcmp(table->modules[i]->name, name) == 0)\n"
        "            return table->modules[i];\n"
        "    }\n"
        "    return NULL;\n"
        "}\n\n",
        spec->name, spec->name, spec->name);

    /* module_get_global */
    vmgen_buf_appendf(&buf,
        "int64_t %s_module_get_global(%s_module_t *mod, size_t index) {\n"
        "    if (index >= %s_MODULE_MAX_GLOBALS) return 0;\n"
        "    return mod->globals[index];\n"
        "}\n\n",
        spec->name, spec->name, upper);

    /* module_set_global */
    vmgen_buf_appendf(&buf,
        "void %s_module_set_global(%s_module_t *mod, size_t index, int64_t value) {\n"
        "    if (index >= %s_MODULE_MAX_GLOBALS) return;\n"
        "    mod->globals[index] = value;\n"
        "}\n",
        spec->name, spec->name, upper);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
