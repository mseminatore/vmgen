#include "validate.h"
#include <stdio.h>
#include <string.h>

int vmgen_validate_spec(const vmgen_spec_t *spec) {
    int errors = 0;

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

    /* Check for duplicate instruction names */
    for (int i = 0; i < spec->instruction_count; i++) {
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

    /* Closures need GC for heap-allocated upvalues */
    if (spec->features.closures && !spec->features.gc_enabled) {
        fprintf(stderr,
                "vmgen: spec warning: closures enabled without GC; "
                "closed upvalues will leak (consider enabling GC)\n");
    }

    /* Coroutines need call frames */
    if (spec->features.coroutines && spec->instruction_count > 0) {
        /* TODO: check that CALL/RETURN instructions exist */
    }

    return errors > 0 ? -1 : 0;
}
