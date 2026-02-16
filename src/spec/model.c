#include "model.h"
#include <string.h>

void vmgen_spec_init(vmgen_spec_t *spec) {
    memset(spec, 0, sizeof(*spec));
    spec->stack.type = VMGEN_STACK_FIXED;
    spec->stack.max_depth = 1024;
    spec->stack.value_type = VMGEN_VALUE_TAGGED_UNION;
    spec->dispatch = VMGEN_DISPATCH_SWITCH;
}

void vmgen_spec_free(vmgen_spec_t *spec) {
    /* Currently all data is inline; nothing to free.
     * This function exists for future heap allocations. */
    (void)spec;
}
