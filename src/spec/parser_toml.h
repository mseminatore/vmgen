#ifndef VMGEN_PARSER_TOML_H
#define VMGEN_PARSER_TOML_H

#include "model.h"

/* Parse a TOML spec file into a vmgen_spec_t.
 * Returns 0 on success, non-zero on error. */
int vmgen_parse_spec_toml(const char *path, vmgen_spec_t *spec);

#endif /* VMGEN_PARSER_TOML_H */
