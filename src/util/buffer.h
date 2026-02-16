#ifndef VMGEN_BUFFER_H
#define VMGEN_BUFFER_H

#include <stddef.h>

/* Dynamic string buffer for code emission */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} vmgen_buf_t;

void vmgen_buf_init(vmgen_buf_t *buf);
void vmgen_buf_free(vmgen_buf_t *buf);
void vmgen_buf_append(vmgen_buf_t *buf, const char *str);
void vmgen_buf_appendf(vmgen_buf_t *buf, const char *fmt, ...);
void vmgen_buf_appendc(vmgen_buf_t *buf, char c);
int  vmgen_buf_write_file(const vmgen_buf_t *buf, const char *path);

#endif /* VMGEN_BUFFER_H */
