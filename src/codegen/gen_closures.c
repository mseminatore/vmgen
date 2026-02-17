#include "gen_closures.h"
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

int vmgen_gen_closures(const vmgen_spec_t *spec, const char *output_dir) {
    if (!spec->features.closures)
        return 0;

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- closures.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/closures.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_CLOSURES_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "closures.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n");
    if (spec->features.gc_enabled) {
        vmgen_buf_append(&buf, "#include \"gc.h\"\n");
    }
    vmgen_buf_appendc(&buf, '\n');

    /* Upvalue struct (with optional GC header) */
    vmgen_buf_appendf(&buf, "typedef struct %s_upvalue {\n", spec->name);
    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf, "    %s_obj_t obj;\n", spec->name);
    }
    vmgen_buf_appendf(&buf,
        "    int64_t *location;\n"
        "    int64_t closed;\n"
        "    int is_closed;\n"
        "    struct %s_upvalue *next;\n"
        "} %s_upvalue_t;\n\n",
        spec->name, spec->name);

    /* Closure struct (with optional GC header) */
    vmgen_buf_appendf(&buf, "typedef struct {\n");
    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf, "    %s_obj_t obj;\n", spec->name);
    }
    vmgen_buf_appendf(&buf,
        "    uint16_t function_index;\n"
        "    %s_upvalue_t **upvalues;\n"
        "    int upvalue_count;\n"
        "} %s_closure_t;\n\n",
        spec->name, spec->name);

    /* API (gc_t* parameter for init when GC enabled) */
    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf,
            "int  %s_init_closure(%s_closure_t *closure, uint16_t fn_index, int upvalue_count, %s_gc_t *gc);\n",
            spec->name, spec->name, spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "int  %s_init_closure(%s_closure_t *closure, uint16_t fn_index, int upvalue_count);\n",
            spec->name, spec->name);
    }
    vmgen_buf_appendf(&buf,
        "void %s_free_closure(%s_closure_t *closure);\n"
        "int64_t %s_get_upvalue(%s_closure_t *closure, int index);\n"
        "void %s_set_upvalue(%s_closure_t *closure, int index, int64_t value);\n"
        "void %s_close_upvalue(%s_upvalue_t *upvalue);\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name);
    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf,
            "void %s_closure_mark(%s_closure_t *closure);\n",
            spec->name, spec->name);
    }

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- closures.c --- */
    snprintf(path, sizeof(path), "%s/closures.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "closures.c", spec);

    vmgen_buf_append(&buf,
        "#include \"closures.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n\n");

    /* init_closure (GC-aware when enabled) */
    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf,
            "int %s_init_closure(%s_closure_t *closure, uint16_t fn_index, int upvalue_count, %s_gc_t *gc) {\n"
            "    closure->function_index = fn_index;\n"
            "    closure->upvalue_count = upvalue_count;\n"
            "    closure->upvalues = NULL;\n"
            "    if (upvalue_count > 0) {\n"
            "        closure->upvalues = (%s_upvalue_t **)calloc((size_t)upvalue_count, sizeof(%s_upvalue_t *));\n"
            "        if (!closure->upvalues) {\n"
            "            fprintf(stderr, \"closures: out of memory\\n\");\n"
            "            return -1;\n"
            "        }\n"
            "        for (int i = 0; i < upvalue_count; i++) {\n"
            "            closure->upvalues[i] = (%s_upvalue_t *)%s_gc_alloc(\n"
            "                gc, %s_OBJ_UPVALUE, sizeof(%s_upvalue_t));\n"
            "            if (!closure->upvalues[i]) return -1;\n"
            "            closure->upvalues[i]->location = NULL;\n"
            "            closure->upvalues[i]->closed = 0;\n"
            "            closure->upvalues[i]->is_closed = 0;\n"
            "            closure->upvalues[i]->next = NULL;\n"
            "        }\n"
            "    }\n"
            "    return 0;\n"
            "}\n\n",
            spec->name, spec->name, spec->name,
            spec->name, spec->name,
            spec->name, spec->name,
            upper, spec->name);
    } else {
        vmgen_buf_appendf(&buf,
            "int %s_init_closure(%s_closure_t *closure, uint16_t fn_index, int upvalue_count) {\n"
            "    closure->function_index = fn_index;\n"
            "    closure->upvalue_count = upvalue_count;\n"
            "    closure->upvalues = NULL;\n"
            "    if (upvalue_count > 0) {\n"
            "        closure->upvalues = (%s_upvalue_t **)calloc((size_t)upvalue_count, sizeof(%s_upvalue_t *));\n"
            "        if (!closure->upvalues) {\n"
            "            fprintf(stderr, \"closures: out of memory\\n\");\n"
            "            return -1;\n"
            "        }\n"
            "    }\n"
            "    return 0;\n"
            "}\n\n",
            spec->name, spec->name,
            spec->name, spec->name);
    }

    /* free_closure */
    vmgen_buf_appendf(&buf,
        "void %s_free_closure(%s_closure_t *closure) {\n"
        "    free(closure->upvalues);\n"
        "    closure->upvalues = NULL;\n"
        "    closure->upvalue_count = 0;\n"
        "}\n\n",
        spec->name, spec->name);

    /* get_upvalue */
    vmgen_buf_appendf(&buf,
        "int64_t %s_get_upvalue(%s_closure_t *closure, int index) {\n"
        "    if (index < 0 || index >= closure->upvalue_count || !closure->upvalues[index])\n"
        "        return 0;\n"
        "    return *closure->upvalues[index]->location;\n"
        "}\n\n",
        spec->name, spec->name);

    /* set_upvalue */
    vmgen_buf_appendf(&buf,
        "void %s_set_upvalue(%s_closure_t *closure, int index, int64_t value) {\n"
        "    if (index < 0 || index >= closure->upvalue_count || !closure->upvalues[index])\n"
        "        return;\n"
        "    *closure->upvalues[index]->location = value;\n"
        "}\n\n",
        spec->name, spec->name);

    /* close_upvalue */
    vmgen_buf_appendf(&buf,
        "void %s_close_upvalue(%s_upvalue_t *upvalue) {\n"
        "    if (!upvalue || upvalue->is_closed) return;\n"
        "    upvalue->closed = *upvalue->location;\n"
        "    upvalue->location = &upvalue->closed;\n"
        "    upvalue->is_closed = 1;\n"
        "}\n",
        spec->name, spec->name);

    /* mark closure + upvalues (when GC enabled) */
    if (spec->features.gc_enabled) {
        vmgen_buf_appendf(&buf,
            "\nvoid %s_closure_mark(%s_closure_t *closure) {\n"
            "    %s_gc_mark_object((%s_obj_t *)closure);\n"
            "    for (int i = 0; i < closure->upvalue_count; i++) {\n"
            "        if (closure->upvalues[i]) {\n"
            "            %s_gc_mark_object((%s_obj_t *)closure->upvalues[i]);\n"
            "        }\n"
            "    }\n"
            "}\n",
            spec->name, spec->name,
            spec->name, spec->name,
            spec->name, spec->name);
    }

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
