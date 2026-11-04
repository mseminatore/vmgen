#include "test.h"
#include "spec/model.h"
#include "spec/parser.h"
#include "spec/parser_toml.h"
#include "spec/validate.h"
#include <string.h>

/* Shared checks for specs/custom_types.yaml and .toml (same content) */
static void check_custom_types_spec(const char *path,
                                    int (*parse)(const char *, vmgen_spec_t *)) {
    vmgen_spec_t ct;
    vmgen_spec_init(&ct);
    int rc = parse(path, &ct);
    TESTEX("parse succeeds", rc == 0);
    TESTEX("built-in types still parsed",
           (ct.type_flags & (VMGEN_TYPE_INT64 | VMGEN_TYPE_OBJECT)) ==
           (VMGEN_TYPE_INT64 | VMGEN_TYPE_OBJECT));
    TESTEX("4 custom types", ct.custom_type_count == 4);
    TESTEX("cell name", strcmp(ct.custom_types[0].name, "cell") == 0);
    TESTEX("cell payload pointer", ct.custom_types[0].payload == VMGEN_PAYLOAD_POINTER);
    TESTEX("cell comment",
           strcmp(ct.custom_types[0].comment, "boxed variable for by-reference capture") == 0);
    TESTEX("handle payload int64", ct.custom_types[1].payload == VMGEN_PAYLOAD_INT64);
    TESTEX("weight payload double", ct.custom_types[2].payload == VMGEN_PAYLOAD_DOUBLE);
    TESTEX("weight defaults gc off", ct.custom_types[2].gc == 0);
    TESTEX("gcbox gc on", ct.custom_types[3].gc == 1);
    TESTEX("custom-type spec validates", vmgen_validate_spec(&ct) == 0);
    vmgen_spec_free(&ct);
}

void test_parser(void) {
    MODULE("Spec Model & Validation");

    /* --- Model initialization --- */
    SUITE("Model Init");

    vmgen_spec_t spec;
    vmgen_spec_init(&spec);

    TEST(spec.stack.type == VMGEN_STACK_FIXED);
    TEST(spec.stack.max_depth == 1024);
    TEST(spec.stack.value_type == VMGEN_VALUE_TAGGED_UNION);
    TEST(spec.dispatch == VMGEN_DISPATCH_SWITCH);
    TEST(spec.instruction_count == 0);
    TEST(spec.name[0] == '\0');
    TEST(spec.pool.type == VMGEN_ALLOC_FIXED);
    TEST(spec.pool.initial_capacity == 256);
    TEST(spec.frame.type == VMGEN_ALLOC_FIXED);
    TEST(spec.frame.max_depth == 64);

    /* --- Validation: empty spec should fail --- */
    SUITE("Validation - Empty Spec");

    TESTEX("empty spec fails validation", vmgen_validate_spec(&spec) != 0);

    /* --- Validation: minimal valid spec --- */
    SUITE("Validation - Minimal Valid Spec");

    snprintf(spec.name, sizeof(spec.name), "testvm");
    snprintf(spec.version, sizeof(spec.version), "1.0");

    /* Add one instruction */
    snprintf(spec.instructions[0].name, VMGEN_MAX_NAME, "HALT");
    spec.instructions[0].operand_count = 0;
    spec.instruction_count = 1;

    TESTEX("minimal spec passes validation", vmgen_validate_spec(&spec) == 0);

    /* --- Validation: duplicate instructions --- */
    SUITE("Validation - Duplicate Instructions");

    snprintf(spec.instructions[1].name, VMGEN_MAX_NAME, "HALT");
    spec.instructions[1].operand_count = 0;
    spec.instruction_count = 2;

    TESTEX("duplicate instruction names fail", vmgen_validate_spec(&spec) != 0);

    vmgen_spec_free(&spec);

    /* --- YAML Parsing: minimal.yaml --- */
    MODULE("YAML Parser");

    SUITE("Parse minimal.yaml");

    vmgen_spec_t parsed;
    vmgen_spec_init(&parsed);
    int rc = vmgen_parse_spec("specs/minimal.yaml", &parsed);
    TESTEX("parse succeeds", rc == 0);
    TESTEX("name is 'minimal'", strcmp(parsed.name, "minimal") == 0);
    TESTEX("version is '1.0'", strcmp(parsed.version, "1.0") == 0);

    SUITE("Stack Config");
    TESTEX("stack type is fixed", parsed.stack.type == VMGEN_STACK_FIXED);
    TESTEX("stack max_depth is 256", parsed.stack.max_depth == 256);
    TESTEX("stack value_type is int64", parsed.stack.value_type == VMGEN_VALUE_INT64);

    SUITE("Types");
    TESTEX("has int64 type", (parsed.type_flags & VMGEN_TYPE_INT64) != 0);
    TESTEX("no double type", (parsed.type_flags & VMGEN_TYPE_DOUBLE) == 0);
    TESTEX("no string type", (parsed.type_flags & VMGEN_TYPE_STRING) == 0);

    SUITE("Features");
    TESTEX("interned_strings is false", parsed.features.interned_strings == 0);
    TESTEX("gc is disabled", parsed.features.gc_enabled == 0);
    TESTEX("closures is false", parsed.features.closures == 0);
    TESTEX("modules is disabled", parsed.features.modules_enabled == 0);
    TESTEX("coroutines is false", parsed.features.coroutines == 0);
    TESTEX("debug_tracing is false", parsed.features.debug_tracing == 0);

    SUITE("Dispatch");
    TESTEX("dispatch is switch", parsed.dispatch == VMGEN_DISPATCH_SWITCH);

    SUITE("Instructions");
    TESTEX("5 instructions parsed", parsed.instruction_count == 5);
    TESTEX("first is PUSH_CONST", strcmp(parsed.instructions[0].name, "PUSH_CONST") == 0);
    TESTEX("PUSH_CONST has 1 operand", parsed.instructions[0].operand_count == 1);
    TESTEX("operand name is 'index'", strcmp(parsed.instructions[0].operands[0].name, "index") == 0);
    TESTEX("operand type is uint16", parsed.instructions[0].operands[0].type == VMGEN_OPERAND_UINT16);
    TESTEX("second is POP", strcmp(parsed.instructions[1].name, "POP") == 0);
    TESTEX("POP has 0 operands", parsed.instructions[1].operand_count == 0);
    TESTEX("third is ADD", strcmp(parsed.instructions[2].name, "ADD") == 0);
    TESTEX("fourth is SUB", strcmp(parsed.instructions[3].name, "SUB") == 0);
    TESTEX("fifth is HALT", strcmp(parsed.instructions[4].name, "HALT") == 0);

    SUITE("Constants");
    TESTEX("has int64 constant", (parsed.const_flags & VMGEN_CONST_INT64) != 0);
    TESTEX("no double constant", (parsed.const_flags & VMGEN_CONST_DOUBLE) == 0);

    SUITE("Parsed spec validates");
    TESTEX("parsed spec passes validation", vmgen_validate_spec(&parsed) == 0);

    vmgen_spec_free(&parsed);

    /* --- Parse full.yaml --- */
    SUITE("Parse full.yaml");

    vmgen_spec_t full;
    vmgen_spec_init(&full);
    rc = vmgen_parse_spec("specs/full.yaml", &full);
    TESTEX("full parse succeeds", rc == 0);
    TESTEX("full name is 'fullvm'", strcmp(full.name, "fullvm") == 0);
    TESTEX("full version is '2.0'", strcmp(full.version, "2.0") == 0);
    TESTEX("full stack is dynamic", full.stack.type == VMGEN_STACK_DYNAMIC);
    TESTEX("full stack depth 4096", full.stack.max_depth == 4096);
    TESTEX("full value_type tagged_union", full.stack.value_type == VMGEN_VALUE_TAGGED_UNION);
    TESTEX("full has all 5 types", full.type_flags == (VMGEN_TYPE_INT64 | VMGEN_TYPE_DOUBLE | VMGEN_TYPE_BOOL | VMGEN_TYPE_STRING | VMGEN_TYPE_OBJECT));
    TESTEX("full interned_strings on", full.features.interned_strings == 1);
    TESTEX("full gc enabled", full.features.gc_enabled == 1);
    TESTEX("full gc mark_sweep", full.features.gc_strategy == VMGEN_GC_MARK_SWEEP);
    TESTEX("full closures on", full.features.closures == 1);
    TESTEX("full modules on", full.features.modules_enabled == 1);
    TESTEX("full modules dynamic", full.features.module_loading == VMGEN_MODULE_DYNAMIC);
    TESTEX("full coroutines on", full.features.coroutines == 1);
    TESTEX("full debug_tracing on", full.features.debug_tracing == 1);
    TESTEX("full dispatch auto", full.dispatch == VMGEN_DISPATCH_AUTO);
    TESTEX("full has 17 instructions", full.instruction_count == 17);
    TESTEX("full CALL has 2 operands", full.instructions[10].operand_count == 2);
    TESTEX("full CALL arity is uint8", full.instructions[10].operands[1].type == VMGEN_OPERAND_UINT8);
    TESTEX("full has 3 const types", full.const_flags == (VMGEN_CONST_INT64 | VMGEN_CONST_DOUBLE | VMGEN_CONST_STRING));

    TESTEX("full pool is dynamic", full.pool.type == VMGEN_ALLOC_DYNAMIC);
    TESTEX("full pool capacity 256", full.pool.initial_capacity == 256);
    TESTEX("full frame is dynamic", full.frame.type == VMGEN_ALLOC_DYNAMIC);
    TESTEX("full frame depth 64", full.frame.max_depth == 64);
    TESTEX("full exception_handling on", full.features.exception_handling == 1);
    TESTEX("TRY_BEGIN is instruction 13", strcmp(full.instructions[13].name, "TRY_BEGIN") == 0);
    TESTEX("THROW is instruction 16", strcmp(full.instructions[16].name, "THROW") == 0);
    TESTEX("full expose_dispatch_table", full.features.expose_dispatch_table == 1);
    TESTEX("ADD has typed_action", full.instructions[2].typed_action_count == 2);
    TESTEX("ADD typed_action[0] is int64", strcmp(full.instructions[2].typed_action_types[0], "int64") == 0);
    TESTEX("ADD typed_action[1] is double", strcmp(full.instructions[2].typed_action_types[1], "double") == 0);
    TESTEX("ADD typed_action[0] DSL", strstr(full.instructions[2].typed_actions[0], "push a + b") != NULL);
    TESTEX("ADD has no plain action", full.instructions[2].action == NULL);
    TESTEX("full has 2 extensions", full.extension_count == 2);
    TESTEX("ext[0] is globals", strcmp(full.extensions[0].name, "globals") == 0);
    TESTEX("ext[0] type void*", strcmp(full.extensions[0].type_str, "void *") == 0);
    TESTEX("ext[0] has comment", strlen(full.extensions[0].comment) > 0);
    TESTEX("ext[1] is user_data", strcmp(full.extensions[1].name, "user_data") == 0);

    TESTEX("full spec validates", vmgen_validate_spec(&full) == 0);
    vmgen_spec_free(&full);

    /* --- Advanced validation checks --- */
    MODULE("Advanced Validation");

    SUITE("Interned strings without string type");
    vmgen_spec_t v;
    vmgen_spec_init(&v);
    snprintf(v.name, sizeof(v.name), "test");
    snprintf(v.version, sizeof(v.version), "1.0");
    snprintf(v.instructions[0].name, VMGEN_MAX_NAME, "HALT");
    v.instruction_count = 1;
    v.features.interned_strings = 1;
    v.type_flags = VMGEN_TYPE_INT64;  /* no string type */
    TESTEX("interned_strings without string type fails",
           vmgen_validate_spec(&v) != 0);

    SUITE("NaN boxing with non-tagged-union");
    vmgen_spec_init(&v);
    snprintf(v.name, sizeof(v.name), "test");
    snprintf(v.version, sizeof(v.version), "1.0");
    snprintf(v.instructions[0].name, VMGEN_MAX_NAME, "HALT");
    v.instruction_count = 1;
    v.features.nan_boxing = 1;
    v.stack.value_type = VMGEN_VALUE_DOUBLE;
    TESTEX("nan_boxing with double value_type fails",
           vmgen_validate_spec(&v) != 0);

    SUITE("Valid minimal spec passes");
    vmgen_spec_init(&v);
    snprintf(v.name, sizeof(v.name), "ok");
    snprintf(v.version, sizeof(v.version), "1.0");
    snprintf(v.instructions[0].name, VMGEN_MAX_NAME, "HALT");
    v.instruction_count = 1;
    TESTEX("clean minimal spec passes", vmgen_validate_spec(&v) == 0);
    vmgen_spec_free(&v);

    /* --- YAML Parsing: nonexistent file --- */
    SUITE("Parse error handling");

    vmgen_spec_t bad;
    vmgen_spec_init(&bad);
    TESTEX("missing file returns error", vmgen_parse_spec("nonexistent.yaml", &bad) != 0);
    vmgen_spec_free(&bad);

    /* --- User-defined value types --- */
    MODULE("Custom Types");

    SUITE("Parse custom_types.yaml");
    check_custom_types_spec("specs/custom_types.yaml", vmgen_parse_spec);
    SUITE("Parse custom_types.toml");
    check_custom_types_spec("specs/custom_types.toml", vmgen_parse_spec_toml);

    /* Validation of custom types (built in memory) */
    #define CT_BASE(s) do { vmgen_spec_init(&(s)); \
        snprintf((s).name, sizeof((s).name), "t"); \
        snprintf((s).version, sizeof((s).version), "1.0"); \
        snprintf((s).instructions[0].name, VMGEN_MAX_NAME, "HALT"); \
        (s).instruction_count = 1; } while (0)

    SUITE("Custom type validation");
    vmgen_spec_t cv;

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "cell");
    cv.custom_type_count = 1;
    TESTEX("valid custom type passes", vmgen_validate_spec(&cv) == 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "Cell");
    cv.custom_type_count = 1;
    TESTEX("upper-case name rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "my-type");
    cv.custom_type_count = 1;
    TESTEX("non-identifier name rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    cv.custom_type_count = 1;  /* empty name */
    TESTEX("empty name rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "object");
    cv.custom_type_count = 1;
    TESTEX("built-in name rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "nil_val");
    cv.custom_type_count = 1;
    TESTEX("generated-macro name rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "cell");
    snprintf(cv.custom_types[1].name, VMGEN_MAX_NAME, "cell");
    cv.custom_type_count = 2;
    TESTEX("duplicate name rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "cell");
    cv.custom_type_count = 1;
    cv.stack.value_type = VMGEN_VALUE_INT64;
    TESTEX("non-tagged-union stack rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    snprintf(cv.custom_types[0].name, VMGEN_MAX_NAME, "num");
    cv.custom_types[0].payload = VMGEN_PAYLOAD_INT64;
    cv.custom_types[0].gc = 1;
    cv.custom_type_count = 1;
    TESTEX("gc on non-pointer payload rejected", vmgen_validate_spec(&cv) != 0);

    CT_BASE(cv);
    cv.custom_type_overflow = 1;
    TESTEX("too many custom types rejected", vmgen_validate_spec(&cv) != 0);
    vmgen_spec_free(&cv);
    #undef CT_BASE
}
