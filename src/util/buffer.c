#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define VMGEN_BUF_INIT_CAP 1024

void vmgen_buf_init(vmgen_buf_t *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void vmgen_buf_free(vmgen_buf_t *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void vmgen_buf_grow(vmgen_buf_t *buf, size_t needed) {
    if (buf->len + needed + 1 <= buf->cap)
        return;

    size_t new_cap = buf->cap ? buf->cap * 2 : VMGEN_BUF_INIT_CAP;
    while (new_cap < buf->len + needed + 1)
        new_cap *= 2;

    char *new_data = realloc(buf->data, new_cap);
    if (!new_data) {
        fprintf(stderr, "vmgen: out of memory\n");
        exit(1);
    }
    buf->data = new_data;
    buf->cap = new_cap;
}

void vmgen_buf_append(vmgen_buf_t *buf, const char *str) {
    size_t slen = strlen(str);
    vmgen_buf_grow(buf, slen);
    memcpy(buf->data + buf->len, str, slen);
    buf->len += slen;
    buf->data[buf->len] = '\0';
}

void vmgen_buf_append_len(vmgen_buf_t *buf, const char *str, size_t len) {
    vmgen_buf_grow(buf, len);
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void vmgen_buf_appendf(vmgen_buf_t *buf, const char *fmt, ...) {
    va_list args;

    /* First pass: determine required size */
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0)
        return;

    vmgen_buf_grow(buf, (size_t)needed);

    /* Second pass: write to buffer */
    va_start(args, fmt);
    vsnprintf(buf->data + buf->len, (size_t)needed + 1, fmt, args);
    va_end(args);

    buf->len += (size_t)needed;
}

void vmgen_buf_appendc(vmgen_buf_t *buf, char c) {
    vmgen_buf_grow(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

int vmgen_buf_write_file(const vmgen_buf_t *buf, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "vmgen: cannot open '%s' for writing\n", path);
        return -1;
    }
    if (buf->len > 0) {
        size_t written = fwrite(buf->data, 1, buf->len, f);
        if (written != buf->len) {
            fprintf(stderr, "vmgen: write error to '%s'\n", path);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}
