#include "test.h"
#include "spec/model.h"
#include "spec/validate.h"

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
}
