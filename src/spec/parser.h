#ifndef VMGEN_PARSER_H
#define VMGEN_PARSER_H

#include "model.h"

/* Parse a YAML spec file into a vmgen_spec_t.
 * Returns 0 on success, non-zero on error. */
int vmgen_parse_spec(const char *path, vmgen_spec_t *spec);

#endif /* VMGEN_PARSER_H */
