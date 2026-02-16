#ifndef VMGEN_GEN_OPCODES_H
#define VMGEN_GEN_OPCODES_H

#include "../spec/model.h"

/* Generate opcodes.h into output_dir */
int vmgen_gen_opcodes(const vmgen_spec_t *spec, const char *output_dir);

#endif /* VMGEN_GEN_OPCODES_H */
