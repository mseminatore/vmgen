#ifndef VMGEN_GEN_SINGLE_HEADER_H
#define VMGEN_GEN_SINGLE_HEADER_H

#include "../spec/model.h"

/* Generate a single amalgamated header file containing all generated code.
 * The implementation is behind #ifdef NAME_IMPLEMENTATION. */
int vmgen_gen_single_header(const vmgen_spec_t *spec, const char *output_dir);

#endif /* VMGEN_GEN_SINGLE_HEADER_H */
