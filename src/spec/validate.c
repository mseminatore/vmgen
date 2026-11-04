#include "validate.h"
#include <stdio.h>
#include <string.h>

/* Lower-case C identifier: [a-z_][a-z0-9_]* */
static int is_lower_ident(const char *s) {
    if (!s || !*s) return 0;
    if (!((*s >= 'a' && *s <= 'z') || *s == '_')) return 0;
    for (const char *c = s + 1; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_'))
            return 0;
    }
    return 1;
}

/* Names that would collide with built-in value types or generated macros
 * (<NAME>_VAL_<TYPE>, <NAME>_<TYPE>(v), <NAME>_NIL_VAL, <NAME>_OBJ(v), ...) */
static int is_reserved_type_name(const char *s) {
    static const char *const reserved[] = {
        "nil", "int64", "double", "bool", "string", "object", "obj",
        "nil_val", "true_val", "false_val", NULL
    };
    for (int i = 0; reserved[i]; i++)
        if (strcmp(s, reserved[i]) == 0) return 1;
    return 0;
}

static int validate_custom_types(const vmgen_spec_t *spec) {
    int errors = 0;
    if (spec->custom_type_overflow) {
        fprintf(stderr, "vmgen: spec error: too many custom types (max %d)\n",
                VMGEN_MAX_CUSTOM_TYPES);
        errors++;
    }
    if (spec->custom_type_count > 0 &&
        spec->stack.value_type != VMGEN_VALUE_TAGGED_UNION) {
        fprintf(stderr, "vmgen: spec error: custom types require value_type "
                "'tagged_union'\n");
        errors++;
    }
    for (int i = 0; i < spec->custom_type_count; i++) {
        const vmgen_custom_type_t *ct = &spec->custom_types[i];
        if (!is_lower_ident(ct->name)) {
            fprintf(stderr, "vmgen: spec error: custom type name '%s' must be a "
                    "lower-case C identifier ([a-z_][a-z0-9_]*)\n", ct->name);
            errors++;
            continue;
        }
        if (is_reserved_type_name(ct->name)) {
            fprintf(stderr, "vmgen: spec error: custom type name '%s' is reserved\n",
                    ct->name);
            errors++;
        }
        for (int j = i + 1; j < spec->custom_type_count; j++) {
            if (strcmp(ct->name, spec->custom_types[j].name) == 0) {
                fprintf(stderr, "vmgen: spec error: duplicate custom type '%s'\n",
                        ct->name);
                errors++;
            }
        }
        if (ct->gc && ct->payload != VMGEN_PAYLOAD_POINTER) {
            fprintf(stderr, "vmgen: spec error: custom type '%s' sets gc but its "
                    "payload is not 'pointer'\n", ct->name);
            errors++;
        }
        if (ct->gc && !(spec->features.gc_enabled &&
                        spec->features.gc_strategy == VMGEN_GC_MARK_SWEEP)) {
            fprintf(stderr, "vmgen: spec warning: custom type '%s' sets gc but "
                    "mark-sweep GC is not enabled; ignoring\n", ct->name);
        }
    }
    return errors;
}

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

    /* Constant pool capacity sanity */
    if (spec->pool.initial_capacity <= 0) {
        fprintf(stderr,
                "vmgen: spec error: constant_pool.initial_capacity must be positive\n");
        errors++;
    }

    /* Frame stack depth sanity */
    if (spec->frame.max_depth <= 0) {
        fprintf(stderr,
                "vmgen: spec error: frames.max_depth must be positive\n");
        errors++;
    }

    /* Exception handling needs tagged_union for exception values */
    if (spec->features.exception_handling &&
        spec->stack.value_type != VMGEN_VALUE_TAGGED_UNION) {
        fprintf(stderr,
                "vmgen: spec warning: exception_handling works best with "
                "value_type 'tagged_union'\n");
        warnings++;
    }

    /* User-defined value types */
    errors += validate_custom_types(spec);

    (void)warnings;
    return errors > 0 ? -1 : 0;
}
