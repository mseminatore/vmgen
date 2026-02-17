#include "gen_gc.h"
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

static int vmgen_gen_gc_marksweep(const vmgen_spec_t *spec, const char *output_dir);
static int vmgen_gen_gc_refcount(const vmgen_spec_t *spec, const char *output_dir);

int vmgen_gen_gc(const vmgen_spec_t *spec, const char *output_dir) {
    if (!spec->features.gc_enabled)
        return 0;

    if (spec->features.gc_strategy == VMGEN_GC_REFCOUNT)
        return vmgen_gen_gc_refcount(spec, output_dir);

    return vmgen_gen_gc_marksweep(spec, output_dir);
}

static int vmgen_gen_gc_marksweep(const vmgen_spec_t *spec, const char *output_dir) {

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- gc.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/gc.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_GC_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "gc.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n\n");

    /* Object type enum */
    vmgen_buf_appendf(&buf,
        "typedef enum {\n"
        "    %s_OBJ_STRING,\n"
        "    %s_OBJ_GENERIC,\n",
        upper, upper);
    if (spec->features.closures) {
        vmgen_buf_appendf(&buf,
            "    %s_OBJ_CLOSURE,\n"
            "    %s_OBJ_UPVALUE,\n",
            upper, upper);
    }
    vmgen_buf_appendf(&buf,
        "} %s_obj_type_t;\n\n", spec->name);

    /* Object header */
    vmgen_buf_appendf(&buf,
        "typedef struct %s_obj {\n"
        "    %s_obj_type_t type;\n"
        "    int marked;\n"
        "    size_t size;\n"
        "    struct %s_obj *next;\n"
        "} %s_obj_t;\n\n",
        spec->name, spec->name, spec->name, spec->name);

    /* GC state */
    vmgen_buf_appendf(&buf,
        "#define %s_GC_INITIAL_THRESHOLD (1024 * 1024)\n\n"
        "typedef void (*%s_gc_mark_roots_fn)(void *user_data);\n\n"
        "typedef struct {\n"
        "    %s_obj_t *objects;\n"
        "    size_t bytes_allocated;\n"
        "    size_t next_gc;\n"
        "    int enabled;\n"
        "    int inhibit_count;\n"
        "    %s_gc_mark_roots_fn mark_roots;\n"
        "    void *roots_data;\n"
        "} %s_gc_t;\n\n",
        upper, spec->name,
        spec->name, spec->name, spec->name);

    /* API */
    vmgen_buf_appendf(&buf,
        "void  %s_gc_init(%s_gc_t *gc);\n"
        "void  %s_gc_free(%s_gc_t *gc);\n"
        "void *%s_gc_alloc(%s_gc_t *gc, %s_obj_type_t type, size_t size);\n"
        "void  %s_gc_mark_object(%s_obj_t *obj);\n"
        "void  %s_gc_collect(%s_gc_t *gc);\n"
        "void  %s_gc_inhibit(%s_gc_t *gc);\n"
        "void  %s_gc_allow(%s_gc_t *gc);\n"
        "void  %s_gc_set_roots(%s_gc_t *gc, %s_gc_mark_roots_fn fn, void *data);\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name);

    vmgen_emit_guard_close(&buf, guard);

    int rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc != 0) return rc;
    printf("vmgen: generated %s\n", path);

    /* --- gc.c --- */
    snprintf(path, sizeof(path), "%s/gc.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "gc.c", spec);

    vmgen_buf_append(&buf,
        "#include \"gc.h\"\n"
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n\n");

    /* init */
    vmgen_buf_appendf(&buf,
        "void %s_gc_init(%s_gc_t *gc) {\n"
        "    gc->objects = NULL;\n"
        "    gc->bytes_allocated = 0;\n"
        "    gc->next_gc = %s_GC_INITIAL_THRESHOLD;\n"
        "    gc->enabled = 1;\n"
        "    gc->inhibit_count = 0;\n"
        "    gc->mark_roots = NULL;\n"
        "    gc->roots_data = NULL;\n"
        "}\n\n",
        spec->name, spec->name, upper);

    /* free all objects */
    vmgen_buf_appendf(&buf,
        "void %s_gc_free(%s_gc_t *gc) {\n"
        "    %s_obj_t *obj = gc->objects;\n"
        "    while (obj) {\n"
        "        %s_obj_t *next = obj->next;\n"
        "        free(obj);\n"
        "        obj = next;\n"
        "    }\n"
        "    gc->objects = NULL;\n"
        "    gc->bytes_allocated = 0;\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name);

    /* alloc */
    vmgen_buf_appendf(&buf,
        "void *%s_gc_alloc(%s_gc_t *gc, %s_obj_type_t type, size_t size) {\n"
        "    if (gc->enabled && gc->inhibit_count == 0 &&\n"
        "        gc->bytes_allocated + size > gc->next_gc) {\n"
        "        %s_gc_collect(gc);\n"
        "    }\n"
        "    %s_obj_t *obj = (%s_obj_t *)malloc(size);\n"
        "    if (!obj) {\n"
        "        fprintf(stderr, \"gc: out of memory\\n\");\n"
        "        return NULL;\n"
        "    }\n"
        "    obj->type = type;\n"
        "    obj->marked = 0;\n"
        "    obj->size = size;\n"
        "    obj->next = gc->objects;\n"
        "    gc->objects = obj;\n"
        "    gc->bytes_allocated += size;\n"
        "    return obj;\n"
        "}\n\n",
        spec->name, spec->name, spec->name,
        spec->name,
        spec->name, spec->name);

    /* mark */
    vmgen_buf_appendf(&buf,
        "void %s_gc_mark_object(%s_obj_t *obj) {\n"
        "    if (!obj || obj->marked) return;\n"
        "    obj->marked = 1;\n"
        "    /* TODO: trace references from this object */\n"
        "}\n\n",
        spec->name, spec->name);

    /* collect: mark roots then sweep */
    vmgen_buf_appendf(&buf,
        "void %s_gc_collect(%s_gc_t *gc) {\n"
        "    if (!gc->enabled || gc->inhibit_count > 0) return;\n\n"
        "    /* Mark phase: let the owner mark all reachable objects */\n"
        "    if (gc->mark_roots) {\n"
        "        gc->mark_roots(gc->roots_data);\n"
        "    }\n\n"
        "    /* Sweep phase: free unmarked objects */\n"
        "    %s_obj_t **obj = &gc->objects;\n"
        "    while (*obj) {\n"
        "        if (!(*obj)->marked) {\n"
        "            %s_obj_t *dead = *obj;\n"
        "            *obj = dead->next;\n"
        "            gc->bytes_allocated -= dead->size;\n"
        "            free(dead);\n"
        "        } else {\n"
        "            (*obj)->marked = 0;\n"
        "            obj = &(*obj)->next;\n"
        "        }\n"
        "    }\n\n"
        "    gc->next_gc = gc->bytes_allocated * 2;\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name);

    /* inhibit/allow */
    vmgen_buf_appendf(&buf,
        "void %s_gc_inhibit(%s_gc_t *gc) {\n"
        "    gc->inhibit_count++;\n"
        "}\n\n"
        "void %s_gc_allow(%s_gc_t *gc) {\n"
        "    if (gc->inhibit_count > 0) gc->inhibit_count--;\n"
        "}\n\n"
        "void %s_gc_set_roots(%s_gc_t *gc, %s_gc_mark_roots_fn fn, void *data) {\n"
        "    gc->mark_roots = fn;\n"
        "    gc->roots_data = data;\n"
        "}\n",
        spec->name, spec->name,
        spec->name, spec->name,
        spec->name, spec->name, spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}

static int vmgen_gen_gc_refcount(const vmgen_spec_t *spec, const char *output_dir) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- gc.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/gc.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_GC_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "gc.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n\n");

    /* Object type enum */
    vmgen_buf_appendf(&buf,
        "typedef enum {\n"
        "    %s_OBJ_STRING,\n"
        "    %s_OBJ_GENERIC,\n",
        upper, upper);
    if (spec->features.closures) {
        vmgen_buf_appendf(&buf,
            "    %s_OBJ_CLOSURE,\n"
            "    %s_OBJ_UPVALUE,\n",
            upper, upper);
    }
    vmgen_buf_appendf(&buf,
        "} %s_obj_type_t;\n\n", spec->name);

    /* Object header with refcount */
    vmgen_buf_appendf(&buf,
        "typedef struct %s_obj {\n"
        "    %s_obj_type_t type;\n"
        "    int refcount;\n"
        "    size_t size;\n"
        "    struct %s_obj *next;\n"
        "} %s_obj_t;\n\n",
        spec->name, spec->name, spec->name, spec->name);

    /* GC state — simpler for refcount */
    vmgen_buf_appendf(&buf,
        "typedef struct {\n"
        "    %s_obj_t *objects;\n"
        "    size_t bytes_allocated;\n"
        "    int enabled;\n"
        "} %s_gc_t;\n\n",
        spec->name, spec->name);

    /* API */
    vmgen_buf_appendf(&buf,
        "void  %s_gc_init(%s_gc_t *gc);\n"
        "void  %s_gc_free(%s_gc_t *gc);\n"
        "void *%s_gc_alloc(%s_gc_t *gc, %s_obj_type_t type, size_t size);\n"
        "void  %s_gc_retain(%s_obj_t *obj);\n"
        "void  %s_gc_release(%s_gc_t *gc, %s_obj_t *obj);\n",
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

    /* --- gc.c --- */
    snprintf(path, sizeof(path), "%s/gc.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "gc.c", spec);

    vmgen_buf_append(&buf,
        "#include \"gc.h\"\n"
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n\n");

    /* init */
    vmgen_buf_appendf(&buf,
        "void %s_gc_init(%s_gc_t *gc) {\n"
        "    gc->objects = NULL;\n"
        "    gc->bytes_allocated = 0;\n"
        "    gc->enabled = 1;\n"
        "}\n\n",
        spec->name, spec->name);

    /* free all objects */
    vmgen_buf_appendf(&buf,
        "void %s_gc_free(%s_gc_t *gc) {\n"
        "    %s_obj_t *obj = gc->objects;\n"
        "    while (obj) {\n"
        "        %s_obj_t *next = obj->next;\n"
        "        free(obj);\n"
        "        obj = next;\n"
        "    }\n"
        "    gc->objects = NULL;\n"
        "    gc->bytes_allocated = 0;\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name);

    /* alloc — starts with refcount 1 */
    vmgen_buf_appendf(&buf,
        "void *%s_gc_alloc(%s_gc_t *gc, %s_obj_type_t type, size_t size) {\n"
        "    %s_obj_t *obj = (%s_obj_t *)malloc(size);\n"
        "    if (!obj) {\n"
        "        fprintf(stderr, \"gc: out of memory\\n\");\n"
        "        return NULL;\n"
        "    }\n"
        "    obj->type = type;\n"
        "    obj->refcount = 1;\n"
        "    obj->size = size;\n"
        "    obj->next = gc->objects;\n"
        "    gc->objects = obj;\n"
        "    gc->bytes_allocated += size;\n"
        "    return obj;\n"
        "}\n\n",
        spec->name, spec->name, spec->name,
        spec->name, spec->name);

    /* retain */
    vmgen_buf_appendf(&buf,
        "void %s_gc_retain(%s_obj_t *obj) {\n"
        "    if (obj) obj->refcount++;\n"
        "}\n\n",
        spec->name, spec->name);

    /* release — frees when refcount drops to 0 */
    vmgen_buf_appendf(&buf,
        "void %s_gc_release(%s_gc_t *gc, %s_obj_t *obj) {\n"
        "    if (!obj) return;\n"
        "    obj->refcount--;\n"
        "    if (obj->refcount > 0) return;\n\n"
        "    /* Unlink from object list */\n"
        "    %s_obj_t **p = &gc->objects;\n"
        "    while (*p) {\n"
        "        if (*p == obj) {\n"
        "            *p = obj->next;\n"
        "            break;\n"
        "        }\n"
        "        p = &(*p)->next;\n"
        "    }\n"
        "    gc->bytes_allocated -= obj->size;\n"
        "    free(obj);\n"
        "}\n",
        spec->name, spec->name, spec->name,
        spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
