#include "validate.h"
#include <stdio.h>
#include <string.h>

static int has_instruction(const vmgen_spec_t *spec, const char *name) {
    for (int i = 0; i < spec->instruction_count; i++) {
        if (strcmp(spec->instructions[i].name, name) == 0)
            return 1;
    }
    return 0;
}

int vmgen_validate_spec(const vmgen_spec_t *spec) {
    int errors = 0;
    int warnings = 0;

    /* Required fields */
    if (spec->name[0] == '\0') {
        fprintf(stderr, "vmgen: spec error: 'vm.name' is required\n");
        errors++;
    }

    if (spec->version[0] == '\0') {
        fprintf(stderr, "vmgen: spec error: 'vm.version' is required\n");
        errors++;
    }

    if (spec->instruction_count == 0) {
        fprintf(stderr, "vmgen: spec error: at least one instruction is required\n");
        errors++;
    }

    /* Stack depth sanity */
    if (spec->stack.max_depth <= 0) {
        fprintf(stderr, "vmgen: spec error: stack.max_depth must be positive\n");
        errors++;
    }

    /* Check for duplicate instruction names */
    for (int i = 0; i < spec->instruction_count; i++) {
        if (spec->instructions[i].name[0] == '\0') {
            fprintf(stderr, "vmgen: spec error: instruction %d has no name\n", i);
            errors++;
            continue;
        }
        for (int j = i + 1; j < spec->instruction_count; j++) {
            if (strcmp(spec->instructions[i].name,
                       spec->instructions[j].name) == 0) {
                fprintf(stderr,
                        "vmgen: spec error: duplicate instruction '%s'\n",
                        spec->instructions[i].name);
                errors++;
            }
        }
    }

    /* Check operand counts */
    for (int i = 0; i < spec->instruction_count; i++) {
        if (spec->instructions[i].operand_count < 0 ||
            spec->instructions[i].operand_count > VMGEN_MAX_OPERANDS) {
            fprintf(stderr,
                    "vmgen: spec error: instruction '%s' has invalid operand count %d\n",
                    spec->instructions[i].name, spec->instructions[i].operand_count);
            errors++;
        }
    }

    /* Feature consistency checks */

    /* Interned strings require string type */
    if (spec->features.interned_strings &&
        !(spec->type_flags & VMGEN_TYPE_STRING)) {
        fprintf(stderr,
                "vmgen: spec error: interned_strings requires 'string' in types\n");
        errors++;
    }

    /* GC requires object or string types to be useful */
    if (spec->features.gc_enabled &&
        !(spec->type_flags & (VMGEN_TYPE_OBJECT | VMGEN_TYPE_STRING))) {
        fprintf(stderr,
                "vmgen: spec warning: GC enabled but no object/string types defined\n");
        warnings++;
    }

    /* Closures need GC for heap-allocated upvalues */
    if (spec->features.closures && !spec->features.gc_enabled) {
        fprintf(stderr,
                "vmgen: spec warning: closures enabled without GC; "
                "closed upvalues will leak (consider enabling GC)\n");
        warnings++;
    }

    /* Modules need CALL/RETURN for cross-module calls */
    if (spec->features.modules_enabled) {
        if (!has_instruction(spec, "CALL") || !has_instruction(spec, "RETURN")) {
            fprintf(stderr,
                    "vmgen: spec warning: modules enabled but CALL/RETURN "
                    "instructions not defined\n");
            warnings++;
        }
    }

    /* Coroutines need CALL/RETURN for fiber function execution */
    if (spec->features.coroutines) {
        if (!has_instruction(spec, "CALL") || !has_instruction(spec, "RETURN")) {
            fprintf(stderr,
                    "vmgen: spec warning: coroutines enabled but CALL/RETURN "
                    "instructions not defined\n");
            warnings++;
        }
    }

    /* NaN boxing only works with tagged_union value type */
    if (spec->features.nan_boxing &&
        spec->stack.value_type != VMGEN_VALUE_TAGGED_UNION) {
        fprintf(stderr,
                "vmgen: spec error: nan_boxing requires value_type 'tagged_union'\n");
        errors++;
    }

    (void)warnings;
    return errors > 0 ? -1 : 0;
}
