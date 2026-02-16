#include "gen_strings.h"
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

int vmgen_gen_strings(const vmgen_spec_t *spec, const char *output_dir) {
    if (!spec->features.interned_strings)
        return 0;

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- strings.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/strings.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_STRINGS_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "strings.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n\n");

    vmgen_buf_appendf(&buf,
        "#define %s_STRING_TABLE_SIZE 256\n\n"
        "typedef struct %s_string {\n"
        "    char *data;\n"
        "    size_t len;\n"
        "    uint32_t hash;\n"
        "    struct %s_string *next;\n"
        "} %s_string_t;\n\n"
        "typedef struct {\n"
        "    %s_string_t *buckets[%s_STRING_TABLE_SIZE];\n"
        "    int count;\n"
        "} %s_string_table_t;\n\n"
        "void %s_string_table_init(%s_string_table_t *table);\n"
        "void %s_string_table_free(%s_string_table_t *table);\n"
        "%s_string_t *%s_string_intern(%s_string_table_t *table, const char *str, size_t len);\n"
        "%s_string_t *%s_string_find(%s_string_table_t *table, const char *str, size_t len);\n",
        upper,
        spec->name, spec->name, spec->name,
        spec->name, upper, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- strings.c --- */
    snprintf(path, sizeof(path), "%s/strings.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "strings.c", spec);

    vmgen_buf_append(&buf,
        "#include \"strings.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n\n");

    /* FNV-1a hash */
    vmgen_buf_appendf(&buf,
        "static uint32_t fnv1a(const char *data, size_t len) {\n"
        "    uint32_t hash = 2166136261u;\n"
        "    for (size_t i = 0; i < len; i++) {\n"
        "        hash ^= (uint8_t)data[i];\n"
        "        hash *= 16777619u;\n"
        "    }\n"
        "    return hash;\n"
        "}\n\n");

    /* init */
    vmgen_buf_appendf(&buf,
        "void %s_string_table_init(%s_string_table_t *table) {\n"
        "    memset(table, 0, sizeof(*table));\n"
        "}\n\n",
        spec->name, spec->name);

    /* free */
    vmgen_buf_appendf(&buf,
        "void %s_string_table_free(%s_string_table_t *table) {\n"
        "    for (int i = 0; i < %s_STRING_TABLE_SIZE; i++) {\n"
        "        %s_string_t *s = table->buckets[i];\n"
        "        while (s) {\n"
        "            %s_string_t *next = s->next;\n"
        "            free(s->data);\n"
        "            free(s);\n"
        "            s = next;\n"
        "        }\n"
        "        table->buckets[i] = NULL;\n"
        "    }\n"
        "    table->count = 0;\n"
        "}\n\n",
        spec->name, spec->name, upper,
        spec->name, spec->name);

    /* find */
    vmgen_buf_appendf(&buf,
        "%s_string_t *%s_string_find(%s_string_table_t *table, const char *str, size_t len) {\n"
        "    uint32_t hash = fnv1a(str, len);\n"
        "    uint32_t idx = hash %% %s_STRING_TABLE_SIZE;\n"
        "    %s_string_t *s = table->buckets[idx];\n"
        "    while (s) {\n"
        "        if (s->hash == hash && s->len == len && memcmp(s->data, str, len) == 0)\n"
        "            return s;\n"
        "        s = s->next;\n"
        "    }\n"
        "    return NULL;\n"
        "}\n\n",
        spec->name, spec->name, spec->name,
        upper, spec->name);

    /* intern */
    vmgen_buf_appendf(&buf,
        "%s_string_t *%s_string_intern(%s_string_table_t *table, const char *str, size_t len) {\n"
        "    %s_string_t *existing = %s_string_find(table, str, len);\n"
        "    if (existing) return existing;\n\n"
        "    %s_string_t *s = (%s_string_t *)malloc(sizeof(%s_string_t));\n"
        "    if (!s) { fprintf(stderr, \"string intern: out of memory\\n\"); return NULL; }\n"
        "    s->data = (char *)malloc(len + 1);\n"
        "    if (!s->data) { free(s); return NULL; }\n"
        "    memcpy(s->data, str, len);\n"
        "    s->data[len] = '\\0';\n"
        "    s->len = len;\n"
        "    s->hash = fnv1a(str, len);\n\n"
        "    uint32_t idx = s->hash %% %s_STRING_TABLE_SIZE;\n"
        "    s->next = table->buckets[idx];\n"
        "    table->buckets[idx] = s;\n"
        "    table->count++;\n"
        "    return s;\n"
        "}\n",
        spec->name, spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        upper);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
