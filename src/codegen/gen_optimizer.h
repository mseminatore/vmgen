#ifndef VMGEN_GEN_OPTIMIZER_H
#define VMGEN_GEN_OPTIMIZER_H

#include "../spec/model.h"

/* Generate optimizer.h and optimizer.c — bytecode peephole optimizer. */
int vmgen_gen_optimizer(const vmgen_spec_t *spec, const char *output_dir);

#endif /* VMGEN_GEN_OPTIMIZER_H */
