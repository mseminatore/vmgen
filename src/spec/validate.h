#ifndef VMGEN_VALIDATE_H
#define VMGEN_VALIDATE_H

#include "model.h"

/* Validate a parsed spec. Returns 0 on success, non-zero on error.
 * Prints error messages to stderr. */
int vmgen_validate_spec(const vmgen_spec_t *spec);

#endif /* VMGEN_VALIDATE_H */
