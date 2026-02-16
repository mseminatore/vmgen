#include "gen_fibers.h"
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

int vmgen_gen_fibers(const vmgen_spec_t *spec, const char *output_dir) {
    if (!spec->features.coroutines)
        return 0;

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* --- fibers.h --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/fibers.h", output_dir);

    char guard[128];
    snprintf(guard, sizeof(guard), "%s_FIBERS_H", upper);

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    vmgen_emit_header(&buf, "fibers.h", spec);
    vmgen_emit_guard_open(&buf, guard);

    vmgen_buf_append(&buf,
        "#include <stdint.h>\n"
        "#include <stddef.h>\n\n");

    /* Fiber status enum */
    vmgen_buf_appendf(&buf,
        "typedef enum {\n"
        "    %s_FIBER_CREATED,\n"
        "    %s_FIBER_RUNNING,\n"
        "    %s_FIBER_SUSPENDED,\n"
        "    %s_FIBER_DEAD,\n"
        "} %s_fiber_status_t;\n\n",
        upper, upper, upper, upper, spec->name);

    /* Fiber struct */
    vmgen_buf_appendf(&buf,
        "#define %s_FIBER_STACK_MAX  256\n"
        "#define %s_FIBER_FRAMES_MAX  64\n\n"
        "typedef struct {\n"
        "    uint64_t stack[%s_FIBER_STACK_MAX];\n"
        "    uint64_t *sp;\n"
        "    uint8_t  frames[%s_FIBER_FRAMES_MAX];\n"
        "    int      frame_count;\n"
        "    %s_fiber_status_t status;\n"
        "    uint64_t result;\n"
        "} %s_fiber_t;\n\n",
        upper, upper,
        upper, upper,
        spec->name, spec->name);

    /* API */
    vmgen_buf_appendf(&buf,
        "%s_fiber_t *%s_fiber_new(void);\n"
        "void  %s_fiber_free(%s_fiber_t *fiber);\n"
        "int   %s_fiber_resume(%s_fiber_t *fiber, uint64_t value);\n"
        "int   %s_fiber_yield(%s_fiber_t *fiber, uint64_t value);\n"
        "%s_fiber_status_t %s_fiber_status(const %s_fiber_t *fiber);\n",
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

    /* --- fibers.c --- */
    snprintf(path, sizeof(path), "%s/fibers.c", output_dir);

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "fibers.c", spec);

    vmgen_buf_append(&buf,
        "#include \"fibers.h\"\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdio.h>\n\n");

    /* fiber_new */
    vmgen_buf_appendf(&buf,
        "%s_fiber_t *%s_fiber_new(void) {\n"
        "    %s_fiber_t *fiber = (%s_fiber_t *)malloc(sizeof(%s_fiber_t));\n"
        "    if (!fiber) {\n"
        "        fprintf(stderr, \"fiber: out of memory\\n\");\n"
        "        return NULL;\n"
        "    }\n"
        "    memset(fiber->stack, 0, sizeof(fiber->stack));\n"
        "    fiber->sp = fiber->stack;\n"
        "    memset(fiber->frames, 0, sizeof(fiber->frames));\n"
        "    fiber->frame_count = 0;\n"
        "    fiber->status = %s_FIBER_CREATED;\n"
        "    fiber->result = 0;\n"
        "    return fiber;\n"
        "}\n\n",
        spec->name, spec->name,
        spec->name, spec->name, spec->name,
        upper);

    /* fiber_free */
    vmgen_buf_appendf(&buf,
        "void %s_fiber_free(%s_fiber_t *fiber) {\n"
        "    if (!fiber) return;\n"
        "    free(fiber);\n"
        "}\n\n",
        spec->name, spec->name);

    /* fiber_resume */
    vmgen_buf_appendf(&buf,
        "int %s_fiber_resume(%s_fiber_t *fiber, uint64_t value) {\n"
        "    if (!fiber) return -1;\n"
        "    if (fiber->status == %s_FIBER_DEAD) return -1;\n"
        "    /* TODO: transfer execution context to fiber */\n"
        "    /* TODO: push value onto fiber stack as resume argument */\n"
        "    fiber->status = %s_FIBER_RUNNING;\n"
        "    (void)value;\n"
        "    return 0;\n"
        "}\n\n",
        spec->name, spec->name,
        upper, upper);

    /* fiber_yield */
    vmgen_buf_appendf(&buf,
        "int %s_fiber_yield(%s_fiber_t *fiber, uint64_t value) {\n"
        "    if (!fiber) return -1;\n"
        "    if (fiber->status != %s_FIBER_RUNNING) return -1;\n"
        "    /* TODO: save execution context and transfer back to caller */\n"
        "    /* TODO: store value as yield result */\n"
        "    fiber->result = value;\n"
        "    fiber->status = %s_FIBER_SUSPENDED;\n"
        "    return 0;\n"
        "}\n\n",
        spec->name, spec->name,
        upper, upper);

    /* fiber_status */
    vmgen_buf_appendf(&buf,
        "%s_fiber_status_t %s_fiber_status(const %s_fiber_t *fiber) {\n"
        "    return fiber->status;\n"
        "}\n",
        spec->name, spec->name, spec->name);

    rc = vmgen_buf_write_file(&buf, path);
    vmgen_buf_free(&buf);
    if (rc == 0) printf("vmgen: generated %s\n", path);
    return rc;
}
