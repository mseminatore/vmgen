#include "model.h"
#include <string.h>
#include <stdlib.h>

void vmgen_spec_init(vmgen_spec_t *spec) {
    memset(spec, 0, sizeof(*spec));
    spec->stack.type = VMGEN_STACK_FIXED;
    spec->stack.max_depth = 1024;
    spec->stack.value_type = VMGEN_VALUE_TAGGED_UNION;
    spec->dispatch = VMGEN_DISPATCH_SWITCH;
    spec->pool.type = VMGEN_ALLOC_FIXED;
    spec->pool.initial_capacity = 256;
    spec->frame.type = VMGEN_ALLOC_FIXED;
    spec->frame.max_depth = 64;
}

void vmgen_spec_free(vmgen_spec_t *spec) {
    for (int i = 0; i < spec->instruction_count; i++) {
        free(spec->instructions[i].action);
        free(spec->instructions[i].action_c);
        for (int j = 0; j < spec->instructions[i].typed_action_count; j++) {
            free(spec->instructions[i].typed_actions[j]);
        }
        free(spec->instructions[i].typed_action_default);
    }
    memset(spec, 0, sizeof(*spec));
}
