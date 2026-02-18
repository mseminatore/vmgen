#include "gen_linemap.h"
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

int vmgen_gen_linemap(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- linemap.h --- */
    vmgen_buf_t hdr;
    vmgen_buf_init(&hdr);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_LINEMAP_H", upper);
    vmgen_emit_header(&hdr, "linemap.h", spec);
    vmgen_emit_guard_open(&hdr, guard);

    vmgen_buf_append(&hdr, "#include <stddef.h>\n\n");

    vmgen_buf_appendf(&hdr,
        "/* A single entry mapping a bytecode offset to a source line */\n"
        "typedef struct {\n"
        "    size_t offset;\n"
        "    int line;\n"
        "} %s_linemap_entry_t;\n\n"
        "/* Line map: dynamically-sized array of offset→line entries */\n"
        "typedef struct {\n"
        "    %s_linemap_entry_t *entries;\n"
        "    int count;\n"
        "    int capacity;\n"
        "} %s_linemap_t;\n\n"
        "void %s_linemap_init(%s_linemap_t *map);\n"
        "void %s_linemap_free(%s_linemap_t *map);\n"
        "void %s_linemap_add(%s_linemap_t *map, size_t offset, int line);\n"
        "int  %s_linemap_get_line(const %s_linemap_t *map, size_t offset);\n",
        spec->name,
        spec->name,
        spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    vmgen_emit_guard_close(&hdr, guard);

    char path[512];
    snprintf(path, sizeof(path), "%s/linemap.h", output_dir);
    int rc = vmgen_buf_write_file(&hdr, path);
    vmgen_buf_free(&hdr);
    if (rc != 0) return rc;

    /* --- linemap.c --- */
    vmgen_buf_t src;
    vmgen_buf_init(&src);
    vmgen_emit_header(&src, "linemap.c", spec);

    vmgen_buf_appendf(&src,
        "#include \"linemap.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n\n"
        "void %s_linemap_init(%s_linemap_t *map) {\n"
        "    memset(map, 0, sizeof(*map));\n"
        "}\n\n"
        "void %s_linemap_free(%s_linemap_t *map) {\n"
        "    free(map->entries);\n"
        "    memset(map, 0, sizeof(*map));\n"
        "}\n\n"
        "void %s_linemap_add(%s_linemap_t *map, size_t offset, int line) {\n"
        "    if (map->count >= map->capacity) {\n"
        "        int new_cap = map->capacity ? map->capacity * 2 : 16;\n"
        "        %s_linemap_entry_t *newbuf = (%s_linemap_entry_t *)realloc(\n"
        "            map->entries, (size_t)new_cap * sizeof(*newbuf));\n"
        "        if (!newbuf) return;\n"
        "        map->entries = newbuf;\n"
        "        map->capacity = new_cap;\n"
        "    }\n"
        "    map->entries[map->count].offset = offset;\n"
        "    map->entries[map->count].line = line;\n"
        "    map->count++;\n"
        "}\n\n"
        "int %s_linemap_get_line(const %s_linemap_t *map, size_t offset) {\n"
        "    /* Binary search for the largest offset <= target */\n"
        "    int lo = 0, hi = map->count - 1;\n"
        "    int result = -1;\n"
        "    while (lo <= hi) {\n"
        "        int mid = lo + (hi - lo) / 2;\n"
        "        if (map->entries[mid].offset <= offset) {\n"
        "            result = map->entries[mid].line;\n"
        "            lo = mid + 1;\n"
        "        } else {\n"
        "            hi = mid - 1;\n"
        "        }\n"
        "    }\n"
        "    return result;\n"
        "}\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);

    snprintf(path, sizeof(path), "%s/linemap.c", output_dir);
    rc = vmgen_buf_write_file(&src, path);
    vmgen_buf_free(&src);
    return rc;
}
