#ifndef VMGEN_GEN_VERIFY_H
#define VMGEN_GEN_VERIFY_H

#include "../spec/model.h"

/* Generate verify.h and verify.c — bytecode static validation. */
int vmgen_gen_verify(const vmgen_spec_t *spec, const char *output_dir);

#endif /* VMGEN_GEN_VERIFY_H */
