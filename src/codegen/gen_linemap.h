#ifndef VMGEN_GEN_LINEMAP_H
#define VMGEN_GEN_LINEMAP_H

#include "../spec/model.h"

/* Generate linemap.h and linemap.c — bytecode offset to source line mapping. */
int vmgen_gen_linemap(const vmgen_spec_t *spec, const char *output_dir);

#endif /* VMGEN_GEN_LINEMAP_H */
