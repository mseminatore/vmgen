#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <yaml.h>

#ifdef _MSC_VER
#define vmgen_strdup _strdup
#else
#define vmgen_strdup strdup
#endif

/* ---- helpers ---- */

static void safe_copy(char *dst, const char *src, size_t max) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

static const char *event_scalar(yaml_event_t *e) {
    return (const char *)e->data.scalar.value;
}

static int str_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int parse_bool(const char *s) {
    if (str_eq(s, "true") || str_eq(s, "yes") || str_eq(s, "on")) return 1;
    return 0;
}

static vmgen_operand_type_t parse_operand_type(const char *s) {
    if (str_eq(s, "uint8"))  return VMGEN_OPERAND_UINT8;
    if (str_eq(s, "uint16")) return VMGEN_OPERAND_UINT16;
    if (str_eq(s, "uint32")) return VMGEN_OPERAND_UINT32;
    if (str_eq(s, "int8"))   return VMGEN_OPERAND_INT8;
    if (str_eq(s, "int16"))  return VMGEN_OPERAND_INT16;
    if (str_eq(s, "int32"))  return VMGEN_OPERAND_INT32;
    fprintf(stderr, "vmgen: unknown operand type '%s'\n", s);
    return VMGEN_OPERAND_UINT8;
}

/* ---- event-based parser state machine ---- */

typedef struct {
    yaml_parser_t parser;
    vmgen_spec_t *spec;
    int error;
} parse_ctx_t;

static int next_event(parse_ctx_t *ctx, yaml_event_t *e) {
    if (!yaml_parser_parse(&ctx->parser, e)) {
        fprintf(stderr, "vmgen: YAML parse error at line %lu: %s\n",
                (unsigned long)ctx->parser.problem_mark.line + 1,
                ctx->parser.problem ? ctx->parser.problem : "unknown");
        ctx->error = 1;
        return 0;
    }
    return 1;
}

/* Skip a YAML value (scalar, mapping, or sequence) */
static void skip_value(parse_ctx_t *ctx) {
    yaml_event_t e;
    if (!next_event(ctx, &e)) return;

    switch (e.type) {
    case YAML_SCALAR_EVENT:
        yaml_event_delete(&e);
        return;
    case YAML_MAPPING_START_EVENT: {
        yaml_event_delete(&e);
        int depth = 1;
        while (depth > 0 && !ctx->error) {
            if (!next_event(ctx, &e)) return;
            if (e.type == YAML_MAPPING_START_EVENT) depth++;
            else if (e.type == YAML_MAPPING_END_EVENT) depth--;
            yaml_event_delete(&e);
        }
        return;
    }
    case YAML_SEQUENCE_START_EVENT: {
        yaml_event_delete(&e);
        int depth = 1;
        while (depth > 0 && !ctx->error) {
            if (!next_event(ctx, &e)) return;
            if (e.type == YAML_SEQUENCE_START_EVENT) depth++;
            else if (e.type == YAML_SEQUENCE_END_EVENT) depth--;
            yaml_event_delete(&e);
        }
        return;
    }
    default:
        yaml_event_delete(&e);
        return;
    }
}

/* Expect a scalar, return its value (caller must yaml_event_delete) */
static int expect_scalar(parse_ctx_t *ctx, yaml_event_t *e) {
    if (!next_event(ctx, e)) return 0;
    if (e->type != YAML_SCALAR_EVENT) {
        fprintf(stderr, "vmgen: expected scalar value\n");
        yaml_event_delete(e);
        ctx->error = 1;
        return 0;
    }
    return 1;
}

/* ---- section parsers ---- */

static void parse_stack(parse_ctx_t *ctx) {
    yaml_event_t e;
    /* already consumed MAPPING_START */
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "type")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            if (str_eq(event_scalar(&v), "dynamic"))
                ctx->spec->stack.type = VMGEN_STACK_DYNAMIC;
            else
                ctx->spec->stack.type = VMGEN_STACK_FIXED;
            yaml_event_delete(&v);
        } else if (str_eq(key, "max_depth")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->stack.max_depth = atoi(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "value_type")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            const char *vt = event_scalar(&v);
            if (str_eq(vt, "double"))
                ctx->spec->stack.value_type = VMGEN_VALUE_DOUBLE;
            else if (str_eq(vt, "int64"))
                ctx->spec->stack.value_type = VMGEN_VALUE_INT64;
            else
                ctx->spec->stack.value_type = VMGEN_VALUE_TAGGED_UNION;
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

/* Parse a user-defined type mapping: { name: cell, payload: pointer, gc: false,
 * comment: "..." }. MAPPING_START has already been consumed. */
static void parse_custom_type(parse_ctx_t *ctx) {
    vmgen_custom_type_t ct;
    memset(&ct, 0, sizeof(ct));
    ct.payload = VMGEN_PAYLOAD_POINTER;
    int bad_payload = 0;

    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); break; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        char key[VMGEN_MAX_NAME];
        safe_copy(key, event_scalar(&e), sizeof(key));
        yaml_event_delete(&e);
        yaml_event_t v;
        if (!expect_scalar(ctx, &v)) return;
        const char *val = event_scalar(&v);

        if (str_eq(key, "name")) {
            safe_copy(ct.name, val, VMGEN_MAX_NAME);
        } else if (str_eq(key, "payload")) {
            if (str_eq(val, "pointer"))     ct.payload = VMGEN_PAYLOAD_POINTER;
            else if (str_eq(val, "int64"))  ct.payload = VMGEN_PAYLOAD_INT64;
            else if (str_eq(val, "double")) ct.payload = VMGEN_PAYLOAD_DOUBLE;
            else {
                fprintf(stderr, "vmgen: unknown payload '%s' for custom type "
                        "(expected pointer, int64 or double)\n", val);
                bad_payload = 1;
            }
        } else if (str_eq(key, "gc")) {
            ct.gc = parse_bool(val);
        } else if (str_eq(key, "comment") || str_eq(key, "description")) {
            safe_copy(ct.comment, val, VMGEN_MAX_DESC);
        } else {
            fprintf(stderr, "vmgen: unknown key '%s' in custom type\n", key);
        }
        yaml_event_delete(&v);
    }

    if (bad_payload) { ctx->error = 1; return; }
    if (ctx->spec->custom_type_count < VMGEN_MAX_CUSTOM_TYPES)
        ctx->spec->custom_types[ctx->spec->custom_type_count++] = ct;
    else
        ctx->spec->custom_type_overflow = 1;
}

static void parse_types(parse_ctx_t *ctx) {
    yaml_event_t e;
    /* already consumed SEQUENCE_START */
    ctx->spec->type_flags = 0;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&e);
            parse_custom_type(ctx);
            continue;
        }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *t = event_scalar(&e);
        if (str_eq(t, "int64"))       ctx->spec->type_flags |= VMGEN_TYPE_INT64;
        else if (str_eq(t, "double")) ctx->spec->type_flags |= VMGEN_TYPE_DOUBLE;
        else if (str_eq(t, "bool"))   ctx->spec->type_flags |= VMGEN_TYPE_BOOL;
        else if (str_eq(t, "string")) ctx->spec->type_flags |= VMGEN_TYPE_STRING;
        else if (str_eq(t, "object")) ctx->spec->type_flags |= VMGEN_TYPE_OBJECT;
        else fprintf(stderr, "vmgen: unknown type '%s'\n", t);
        yaml_event_delete(&e);
    }
}

static void parse_gc(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "enabled")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.gc_enabled = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "strategy")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            const char *s = event_scalar(&v);
            if (str_eq(s, "refcount"))
                ctx->spec->features.gc_strategy = VMGEN_GC_REFCOUNT;
            else
                ctx->spec->features.gc_strategy = VMGEN_GC_MARK_SWEEP;
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_modules(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "enabled")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.modules_enabled = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "loading")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            const char *s = event_scalar(&v);
            if (str_eq(s, "dynamic"))
                ctx->spec->features.module_loading = VMGEN_MODULE_DYNAMIC;
            else if (str_eq(s, "both"))
                ctx->spec->features.module_loading = VMGEN_MODULE_BOTH;
            else
                ctx->spec->features.module_loading = VMGEN_MODULE_STATIC;
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_features(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "interned_strings")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.interned_strings = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "garbage_collection")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_gc(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "closures")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.closures = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "modules")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_modules(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "coroutines")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.coroutines = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "debug_tracing")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.debug_tracing = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "nan_boxing")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.nan_boxing = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "exception_handling")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.exception_handling = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "expose_dispatch_table")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->features.expose_dispatch_table = parse_bool(event_scalar(&v));
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_operand(parse_ctx_t *ctx, vmgen_operand_t *op) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "name")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            safe_copy(op->name, event_scalar(&v), VMGEN_MAX_NAME);
            yaml_event_delete(&v);
        } else if (str_eq(key, "type")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            op->type = parse_operand_type(event_scalar(&v));
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_operands(parse_ctx_t *ctx, vmgen_instr_t *instr) {
    yaml_event_t e;
    instr->operand_count = 0;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&e);
            if (instr->operand_count < VMGEN_MAX_OPERANDS) {
                parse_operand(ctx, &instr->operands[instr->operand_count]);
                instr->operand_count++;
            } else {
                /* skip extra operands */
                int depth = 1;
                while (depth > 0 && !ctx->error) {
                    if (!next_event(ctx, &e)) return;
                    if (e.type == YAML_MAPPING_START_EVENT) depth++;
                    else if (e.type == YAML_MAPPING_END_EVENT) depth--;
                    yaml_event_delete(&e);
                }
            }
        } else {
            yaml_event_delete(&e);
        }
    }
}

static void parse_instruction(parse_ctx_t *ctx, vmgen_instr_t *instr) {
    yaml_event_t e;
    memset(instr, 0, sizeof(*instr));
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "name")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            safe_copy(instr->name, event_scalar(&v), VMGEN_MAX_NAME);
            yaml_event_delete(&v);
        } else if (str_eq(key, "description")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            safe_copy(instr->description, event_scalar(&v), VMGEN_MAX_DESC);
            yaml_event_delete(&v);
        } else if (str_eq(key, "operands")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&v);
                parse_operands(ctx, instr);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "action")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            free(instr->action);
            instr->action = vmgen_strdup(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "action_c")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            free(instr->action_c);
            instr->action_c = vmgen_strdup(event_scalar(&v));
            yaml_event_delete(&v);
        } else if (str_eq(key, "stack_effect")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            instr->stack_effect = atoi(event_scalar(&v));
            instr->stack_effect_set = true;
            yaml_event_delete(&v);
        } else if (str_eq(key, "typed_action")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                /* Parse typed_action mapping: { type: "dsl", default: "inline C" } */
                yaml_event_t te;
                while (!ctx->error) {
                    if (!next_event(ctx, &te)) return;
                    if (te.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&te); break; }
                    if (te.type != YAML_SCALAR_EVENT) { yaml_event_delete(&te); continue; }
                    const char *tkey = event_scalar(&te);
                    int is_default = str_eq(tkey, "default");
                    char type_name[VMGEN_MAX_NAME];
                    if (!is_default)
                        safe_copy(type_name, tkey, VMGEN_MAX_NAME);
                    yaml_event_delete(&te);
                    yaml_event_t tv;
                    if (!expect_scalar(ctx, &tv)) return;
                    if (is_default) {
                        free(instr->typed_action_default);
                        instr->typed_action_default = vmgen_strdup(event_scalar(&tv));
                    } else if (instr->typed_action_count < VMGEN_MAX_TYPED_ACTIONS) {
                        int idx = instr->typed_action_count++;
                        safe_copy(instr->typed_action_types[idx], type_name, VMGEN_MAX_NAME);
                        instr->typed_actions[idx] = vmgen_strdup(event_scalar(&tv));
                    }
                    yaml_event_delete(&tv);
                }
            } else {
                yaml_event_delete(&v);
            }
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_instructions(parse_ctx_t *ctx) {
    yaml_event_t e;
    ctx->spec->instruction_count = 0;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&e);
            if (ctx->spec->instruction_count < VMGEN_MAX_INSTR) {
                parse_instruction(ctx,
                    &ctx->spec->instructions[ctx->spec->instruction_count]);
                ctx->spec->instruction_count++;
            } else {
                fprintf(stderr, "vmgen: too many instructions (max %d)\n",
                        VMGEN_MAX_INSTR);
                ctx->error = 1;
                return;
            }
        } else {
            yaml_event_delete(&e);
        }
    }
}

static void parse_constants(parse_ctx_t *ctx) {
    yaml_event_t e;
    ctx->spec->const_flags = 0;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *t = event_scalar(&e);
        if (str_eq(t, "int64"))       ctx->spec->const_flags |= VMGEN_CONST_INT64;
        else if (str_eq(t, "double")) ctx->spec->const_flags |= VMGEN_CONST_DOUBLE;
        else if (str_eq(t, "string")) ctx->spec->const_flags |= VMGEN_CONST_STRING;
        else fprintf(stderr, "vmgen: unknown constant type '%s'\n", t);
        yaml_event_delete(&e);
    }
}

static void parse_constant_pool(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "type")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            if (str_eq(event_scalar(&v), "dynamic"))
                ctx->spec->pool.type = VMGEN_ALLOC_DYNAMIC;
            else
                ctx->spec->pool.type = VMGEN_ALLOC_FIXED;
            yaml_event_delete(&v);
        } else if (str_eq(key, "initial_capacity")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->pool.initial_capacity = atoi(event_scalar(&v));
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_frames(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "type")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            if (str_eq(event_scalar(&v), "dynamic"))
                ctx->spec->frame.type = VMGEN_ALLOC_DYNAMIC;
            else
                ctx->spec->frame.type = VMGEN_ALLOC_FIXED;
            yaml_event_delete(&v);
        } else if (str_eq(key, "max_depth")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            ctx->spec->frame.max_depth = atoi(event_scalar(&v));
            yaml_event_delete(&v);
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

/* Parse a single extension: { name: ..., type: ..., include: ..., comment: ... } */
static void parse_extension(parse_ctx_t *ctx) {
    vmgen_extension_t ext;
    memset(&ext, 0, sizeof(ext));

    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); break; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        int is_name    = str_eq(key, "name");
        int is_type    = str_eq(key, "type");
        int is_include = str_eq(key, "include");
        int is_comment = str_eq(key, "comment");
        yaml_event_delete(&e);
        yaml_event_t v;
        if (!expect_scalar(ctx, &v)) return;
        const char *val = event_scalar(&v);

        if (is_name)         safe_copy(ext.name,     val, VMGEN_MAX_NAME);
        else if (is_type)    safe_copy(ext.type_str, val, VMGEN_MAX_DESC);
        else if (is_include) safe_copy(ext.include,  val, VMGEN_MAX_NAME);
        else if (is_comment) safe_copy(ext.comment,  val, VMGEN_MAX_DESC);
        yaml_event_delete(&v);
    }

    if (ext.name[0] && ext.type_str[0]) {
        if (ctx->spec->extension_count < VMGEN_MAX_EXTENSIONS) {
            ctx->spec->extensions[ctx->spec->extension_count++] = ext;
        }
    }
}

/* Parse vm_extensions: [ { name: ..., type: ... }, ... ] */
static void parse_vm_extensions(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_SEQUENCE_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&e);
            parse_extension(ctx);
        } else {
            yaml_event_delete(&e);
        }
    }
}

static void parse_vm(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "name")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            safe_copy(ctx->spec->name, event_scalar(&v), VMGEN_MAX_NAME);
            yaml_event_delete(&v);
        } else if (str_eq(key, "version")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            safe_copy(ctx->spec->version, event_scalar(&v), VMGEN_MAX_VERSION);
            yaml_event_delete(&v);
        } else if (str_eq(key, "stack")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_stack(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "types")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&v);
                parse_types(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "features")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_features(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "dispatch")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!expect_scalar(ctx, &v)) return;
            const char *d = event_scalar(&v);
            if (str_eq(d, "fnptr"))
                ctx->spec->dispatch = VMGEN_DISPATCH_FNPTR;
            else if (str_eq(d, "threaded"))
                ctx->spec->dispatch = VMGEN_DISPATCH_THREADED;
            else if (str_eq(d, "auto"))
                ctx->spec->dispatch = VMGEN_DISPATCH_AUTO;
            else
                ctx->spec->dispatch = VMGEN_DISPATCH_SWITCH;
            yaml_event_delete(&v);
        } else if (str_eq(key, "instructions")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&v);
                parse_instructions(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "constants")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&v);
                parse_constants(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "constant_pool")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_constant_pool(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "frames")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_frames(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else if (str_eq(key, "vm_extensions")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_SEQUENCE_START_EVENT) {
                yaml_event_delete(&v);
                parse_vm_extensions(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

static void parse_root(parse_ctx_t *ctx) {
    yaml_event_t e;
    while (!ctx->error) {
        if (!next_event(ctx, &e)) return;
        if (e.type == YAML_MAPPING_END_EVENT) { yaml_event_delete(&e); return; }
        if (e.type != YAML_SCALAR_EVENT) { yaml_event_delete(&e); continue; }

        const char *key = event_scalar(&e);
        if (str_eq(key, "vm")) {
            yaml_event_delete(&e);
            yaml_event_t v;
            if (!next_event(ctx, &v)) return;
            if (v.type == YAML_MAPPING_START_EVENT) {
                yaml_event_delete(&v);
                parse_vm(ctx);
            } else {
                yaml_event_delete(&v);
            }
        } else {
            yaml_event_delete(&e);
            skip_value(ctx);
        }
    }
}

/* ---- public API ---- */

int vmgen_parse_spec(const char *path, vmgen_spec_t *spec) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "vmgen: cannot open spec file '%s'\n", path);
        return -1;
    }

    parse_ctx_t ctx;
    ctx.spec = spec;
    ctx.error = 0;

    if (!yaml_parser_initialize(&ctx.parser)) {
        fprintf(stderr, "vmgen: failed to initialize YAML parser\n");
        fclose(f);
        return -1;
    }

    yaml_parser_set_input_file(&ctx.parser, f);

    /* Consume STREAM_START */
    yaml_event_t e;
    if (!next_event(&ctx, &e)) goto fail;
    if (e.type != YAML_STREAM_START_EVENT) {
        yaml_event_delete(&e);
        goto fail;
    }
    yaml_event_delete(&e);

    /* Consume DOCUMENT_START */
    if (!next_event(&ctx, &e)) goto fail;
    if (e.type != YAML_DOCUMENT_START_EVENT) {
        yaml_event_delete(&e);
        goto fail;
    }
    yaml_event_delete(&e);

    /* Consume root MAPPING_START */
    if (!next_event(&ctx, &e)) goto fail;
    if (e.type != YAML_MAPPING_START_EVENT) {
        fprintf(stderr, "vmgen: expected root mapping in spec\n");
        yaml_event_delete(&e);
        goto fail;
    }
    yaml_event_delete(&e);

    parse_root(&ctx);

    yaml_parser_delete(&ctx.parser);
    fclose(f);
    return ctx.error ? -1 : 0;

fail:
    yaml_parser_delete(&ctx.parser);
    fclose(f);
    return -1;
}
