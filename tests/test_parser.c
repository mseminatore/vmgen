#include "test.h"
#include "spec/model.h"
#include "spec/parser.h"
#include "spec/validate.h"
#include <string.h>

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

    /* --- YAML Parsing: nonexistent file --- */
    SUITE("Parse error handling");

    vmgen_spec_t bad;
    vmgen_spec_init(&bad);
    TESTEX("missing file returns error", vmgen_parse_spec("nonexistent.yaml", &bad) != 0);
    vmgen_spec_free(&bad);
}
