#ifndef VMGEN_ACTION_EMIT_H
#define VMGEN_ACTION_EMIT_H

#include "action_parser.h"
#include "../util/buffer.h"
#include "../spec/model.h"

/* Emit C code for a parsed DSL action into buf.
 * indent: indentation prefix string (e.g. "            ")
 * spec: the VM spec (for name prefix and value type)
 * instr: the instruction (for operand names cross-reference) */
int vmgen_action_emit(vmgen_buf_t *buf, const char *indent,
                      const vmgen_spec_t *spec, const vmgen_instr_t *instr,
                      const vmgen_action_t *action);

/* Emit type-dispatched action blocks for a tagged union VM.
 * Generates if/else-if chain checking operand types, with per-type
 * DSL bodies and optional default fallback (inline C). */
int vmgen_action_emit_typed(vmgen_buf_t *buf, const char *indent,
                            const vmgen_spec_t *spec, const vmgen_instr_t *instr);

/* Auto-dispatch: when a plain action: has arithmetic and spec supports
 * both int64 and double, auto-generate type-dispatched code.
 * Returns 0 if auto-dispatch was applied, -1 if not applicable. */
int vmgen_action_auto_dispatch(vmgen_buf_t *buf, const char *indent,
                               const vmgen_spec_t *spec,
                               const vmgen_instr_t *instr,
                               const vmgen_action_t *action);

/* Emit inline C code (verbatim, with indentation per line). */
void vmgen_action_emit_inline_c(vmgen_buf_t *buf, const char *indent,
                                const char *code);

#endif /* VMGEN_ACTION_EMIT_H */
