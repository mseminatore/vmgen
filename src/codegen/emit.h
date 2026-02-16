#ifndef VMGEN_EMIT_H
#define VMGEN_EMIT_H

#include "../spec/model.h"
#include "../util/buffer.h"

/* Emit a C file header comment */
void vmgen_emit_header(vmgen_buf_t *buf, const char *filename,
                       const vmgen_spec_t *spec);

/* Emit an include guard open */
void vmgen_emit_guard_open(vmgen_buf_t *buf, const char *guard_name);

/* Emit an include guard close */
void vmgen_emit_guard_close(vmgen_buf_t *buf, const char *guard_name);

#endif /* VMGEN_EMIT_H */
