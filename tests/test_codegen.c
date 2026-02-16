#include "test.h"
#include "util/buffer.h"
#include "codegen/emit.h"
#include "spec/model.h"

void test_codegen(void) {
    MODULE("Code Generation Utilities");

    /* --- Buffer operations --- */
    SUITE("Buffer");

    vmgen_buf_t buf;
    vmgen_buf_init(&buf);

    TEST(buf.data == NULL);
    TEST(buf.len == 0);
    TEST(buf.cap == 0);

    vmgen_buf_append(&buf, "hello");
    TEST(buf.len == 5);
    TESTEX("buffer contains 'hello'", strcmp(buf.data, "hello") == 0);

    vmgen_buf_append(&buf, " world");
    TEST(buf.len == 11);
    TESTEX("buffer contains 'hello world'", strcmp(buf.data, "hello world") == 0);

    vmgen_buf_appendc(&buf, '!');
    TEST(buf.len == 12);

    vmgen_buf_free(&buf);
    TEST(buf.data == NULL);
    TEST(buf.len == 0);

    /* --- Format append --- */
    SUITE("Buffer Format");

    vmgen_buf_init(&buf);
    vmgen_buf_appendf(&buf, "value = %d", 42);
    TESTEX("formatted append", strcmp(buf.data, "value = 42") == 0);
    vmgen_buf_free(&buf);

    /* --- Emit helpers --- */
    SUITE("Emit Helpers");

    vmgen_spec_t spec;
    vmgen_spec_init(&spec);
    snprintf(spec.name, sizeof(spec.name), "testvm");
    snprintf(spec.version, sizeof(spec.version), "1.0");

    vmgen_buf_init(&buf);
    vmgen_emit_header(&buf, "test.h", &spec);
    TESTEX("header contains filename", strstr(buf.data, "test.h") != NULL);
    TESTEX("header contains vm name", strstr(buf.data, "testvm") != NULL);
    vmgen_buf_free(&buf);

    vmgen_buf_init(&buf);
    vmgen_emit_guard_open(&buf, "TEST_GUARD_H");
    TESTEX("guard open has #ifndef", strstr(buf.data, "#ifndef TEST_GUARD_H") != NULL);
    TESTEX("guard open has #define", strstr(buf.data, "#define TEST_GUARD_H") != NULL);
    vmgen_emit_guard_close(&buf, "TEST_GUARD_H");
    TESTEX("guard close has #endif", strstr(buf.data, "#endif") != NULL);
    vmgen_buf_free(&buf);

    vmgen_spec_free(&spec);
}
