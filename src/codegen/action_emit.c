#include "action_emit.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Check if spec uses tagged union values */
static int is_tagged_union(const vmgen_spec_t *spec) {
    return spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION;
}

/* Type context for typed dispatch emission */
typedef enum {
    TYPE_CTX_INT64,
    TYPE_CTX_DOUBLE,
} type_ctx_t;

/* Get the scalar accessor for a type context */
static const char *type_ctx_accessor(type_ctx_t ctx) {
    return ctx == TYPE_CTX_DOUBLE ? ".as.f64" : ".as.i64";
}

/* Get the constructor macro name suffix for a type context */
static const char *type_ctx_constructor(type_ctx_t ctx) {
    return ctx == TYPE_CTX_DOUBLE ? "DOUBLE" : "INT64";
}

/* Get the val_type enum suffix for a type context */
static const char *type_ctx_val_type(type_ctx_t ctx) {
    return ctx == TYPE_CTX_DOUBLE ? "VAL_DOUBLE" : "VAL_INT64";
}

/* Map type name string to type context. Returns -1 if unknown/unsupported. */
static int type_name_to_ctx(const char *name, type_ctx_t *out) {
    if (strcmp(name, "int64") == 0)  { *out = TYPE_CTX_INT64;  return 0; }
    if (strcmp(name, "double") == 0) { *out = TYPE_CTX_DOUBLE; return 0; }
    return -1;
}

/* Type-aware expression emitter: uses type context to determine accessor.
 * extract_scalar: when 1, appends type-appropriate accessor (.as.i64 or .as.f64)
 * When 0, values are emitted as full tagged unions (for passthrough). */
static void emit_expr_typed(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                            const vmgen_expr_t *e, int extract_scalar,
                            type_ctx_t tctx) {
    if (!e) return;

    switch (e->type) {
    case EXPR_NUMBER:
        vmgen_buf_appendf(buf, "%lld", e->u.number);
        break;

    case EXPR_IDENT:
        vmgen_buf_append(buf, e->u.ident);
        if (extract_scalar && is_tagged_union(spec))
            vmgen_buf_append(buf, type_ctx_accessor(tctx));
        break;

    case EXPR_OPERAND:
        vmgen_buf_append(buf, e->u.ident);
        break;

    case EXPR_CONST_INDEX:
        vmgen_buf_appendf(buf, "vm->constants.values[");
        /* Index is always int */
        emit_expr_typed(buf, spec, e->u.index, 1, TYPE_CTX_INT64);
        vmgen_buf_append(buf, "]");
        if (extract_scalar && is_tagged_union(spec))
            vmgen_buf_append(buf, type_ctx_accessor(tctx));
        break;

    case EXPR_BINOP: {
        const char *op_str = "+";
        switch (e->u.binop.op) {
        case OP_ADD: op_str = "+";  break;
        case OP_SUB: op_str = "-";  break;
        case OP_MUL: op_str = "*";  break;
        case OP_DIV: op_str = "/";  break;
        case OP_MOD: op_str = "%"; break;
        case OP_EQ:  op_str = "=="; break;
        case OP_NEQ: op_str = "!="; break;
        case OP_LT:  op_str = "<";  break;
        case OP_GT:  op_str = ">";  break;
        case OP_LE:  op_str = "<="; break;
        case OP_GE:  op_str = ">="; break;
        default: break;
        }
        vmgen_buf_append(buf, "(");
        emit_expr_typed(buf, spec, e->u.binop.left, 1, tctx);
        vmgen_buf_append(buf, " ");
        vmgen_buf_append(buf, op_str);
        vmgen_buf_append(buf, " ");
        emit_expr_typed(buf, spec, e->u.binop.right, 1, tctx);
        vmgen_buf_append(buf, ")");
        break;
    }

    case EXPR_UNARY:
        if (e->u.unary.op == OP_NEG) {
            vmgen_buf_append(buf, "(-(");
            emit_expr_typed(buf, spec, e->u.unary.operand, 1, tctx);
            vmgen_buf_append(buf, "))");
        } else {
            vmgen_buf_append(buf, "(!(");
            emit_expr_typed(buf, spec, e->u.unary.operand, 1, tctx);
            vmgen_buf_append(buf, "))");
        }
        break;
    }
}

/* Emit expression with int64 type context (default) */
static void emit_expr_ex(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                         const vmgen_expr_t *e, int extract_scalar) {
    emit_expr_typed(buf, spec, e, extract_scalar, TYPE_CTX_INT64);
}

/* Default: extract scalar for int64 arithmetic contexts */
static void emit_expr(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                      const vmgen_expr_t *e) {
    emit_expr_typed(buf, spec, e, 1, TYPE_CTX_INT64);
}

/* Get the C type string for the VM's value type. */
static const char *value_c_type(const vmgen_spec_t *spec) {
    switch (spec->stack.value_type) {
    case VMGEN_VALUE_DOUBLE: return "double";
    case VMGEN_VALUE_INT64:  return "int64_t";
    case VMGEN_VALUE_TAGGED_UNION: return "int64_t";
    default:                 return "int64_t";
    }
}

static void make_upper(char *dst, const char *src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

int vmgen_action_emit(vmgen_buf_t *buf, const char *indent,
                      const vmgen_spec_t *spec, const vmgen_instr_t *instr,
                      const vmgen_action_t *action) {
    (void)instr;
    const char *vtype = value_c_type(spec);

    for (int i = 0; i < action->count; i++) {
        const vmgen_act_stmt_t *s = &action->stmts[i];

        switch (s->type) {
        case ACT_POP:
            if (is_tagged_union(spec)) {
                if (strcmp(s->ident, "_") == 0) {
                    vmgen_buf_appendf(buf, "%s(void)%s_vm_pop(vm);\n",
                                      indent, spec->name);
                } else {
                    vmgen_buf_appendf(buf, "%s%s_value_t %s = %s_vm_pop(vm);\n",
                                      indent, spec->name, s->ident, spec->name);
                }
            } else {
                vmgen_buf_appendf(buf, "%s%s %s = %s_vm_pop(vm);\n",
                                  indent, vtype, s->ident, spec->name);
                if (strcmp(s->ident, "_") == 0) {
                    vmgen_buf_appendf(buf, "%s(void)_;\n", indent);
                }
            }
            break;

        case ACT_PUSH:
            if (is_tagged_union(spec)) {
                if (s->expr && (s->expr->type == EXPR_IDENT ||
                                s->expr->type == EXPR_CONST_INDEX)) {
                    /* Passthrough: value is already a tagged union */
                    vmgen_buf_appendf(buf, "%s%s_vm_push(vm, ", indent, spec->name);
                    emit_expr_ex(buf, spec, s->expr, 0);
                    vmgen_buf_append(buf, ");\n");
                } else {
                    /* Arithmetic result: wrap in INT64 */
                    char upper[VMGEN_MAX_NAME];
                    make_upper(upper, spec->name, sizeof(upper));
                    vmgen_buf_appendf(buf, "%s%s_vm_push(vm, %s_INT64(",
                                      indent, spec->name, upper);
                    emit_expr(buf, spec, s->expr);
                    vmgen_buf_append(buf, "));\n");
                }
            } else {
                vmgen_buf_appendf(buf, "%s%s_vm_push(vm, ", indent, spec->name);
                emit_expr(buf, spec, s->expr);
                vmgen_buf_append(buf, ");\n");
            }
            break;

        case ACT_PEEK:
            if (is_tagged_union(spec)) {
                vmgen_buf_appendf(buf, "%s%s_value_t %s = vm->stack[vm->sp - 1];\n",
                                  indent, spec->name, s->ident);
            } else {
                vmgen_buf_appendf(buf, "%s%s %s = vm->stack[vm->sp - 1];\n",
                                  indent, vtype, s->ident);
            }
            break;

        case ACT_JUMP:
            vmgen_buf_appendf(buf, "%svm->ip += ", indent);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ";\n");
            break;

        case ACT_JUMP_IF_ZERO:
            if (is_tagged_union(spec))
                vmgen_buf_appendf(buf, "%sif (%s.as.i64 == 0) vm->ip += ", indent, s->ident);
            else
                vmgen_buf_appendf(buf, "%sif (%s == 0) vm->ip += ", indent, s->ident);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ";\n");
            break;

        case ACT_JUMP_IF_NONZERO:
            if (is_tagged_union(spec))
                vmgen_buf_appendf(buf, "%sif (%s.as.i64 != 0) vm->ip += ", indent, s->ident);
            else
                vmgen_buf_appendf(buf, "%sif (%s != 0) vm->ip += ", indent, s->ident);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ";\n");
            break;

        case ACT_HALT:
            vmgen_buf_appendf(buf, "%sreturn 0;\n", indent);
            break;

        case ACT_RETURN:
            vmgen_buf_appendf(buf, "%sif (vm->frame_count == 0) return 0;\n", indent);
            vmgen_buf_appendf(buf, "%svm->frame_count--;\n", indent);
            vmgen_buf_appendf(buf, "%svm->ip = vm->frames[vm->frame_count].return_ip;\n", indent);
            vmgen_buf_appendf(buf, "%svm->sp = vm->frames[vm->frame_count].base_sp;\n", indent);
            break;

        case ACT_CALL:
            vmgen_buf_appendf(buf, "%s/* call func_index=", indent);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ", arity=");
            emit_expr(buf, spec, s->expr2);
            vmgen_buf_append(buf, " */\n");
            vmgen_buf_appendf(buf, "%svm->frames[vm->frame_count].return_ip = vm->ip;\n", indent);
            vmgen_buf_appendf(buf, "%svm->frames[vm->frame_count].base_sp = vm->sp - ", indent);
            emit_expr(buf, spec, s->expr2);
            vmgen_buf_append(buf, ";\n");
            vmgen_buf_appendf(buf, "%svm->frames[vm->frame_count].func_index = ", indent);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ";\n");
            vmgen_buf_appendf(buf, "%svm->frame_count++;\n", indent);
            break;

        case ACT_SET:
            vmgen_buf_appendf(buf, "%s%s %s = ", indent, vtype, s->ident);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ";\n");
            break;

        case ACT_TRY_BEGIN: {
            char upper[VMGEN_MAX_NAME];
            make_upper(upper, spec->name, sizeof(upper));
            vmgen_buf_appendf(buf, "%s/* push exception frame */\n", indent);
            vmgen_buf_appendf(buf, "%svm->exception_frames[vm->exception_count].handler_ip = vm->ip + ",
                              indent);
            emit_expr(buf, spec, s->expr);
            vmgen_buf_append(buf, ";\n");
            vmgen_buf_appendf(buf,
                "%svm->exception_frames[vm->exception_count].saved_sp = vm->sp;\n"
                "%svm->exception_frames[vm->exception_count].saved_frame_count = vm->frame_count;\n"
                "%svm->exception_count++;\n",
                indent, indent, indent);
            break;
        }

        case ACT_TRY_END:
            vmgen_buf_appendf(buf,
                "%s/* pop exception frame */\n"
                "%sif (vm->exception_count > 0) vm->exception_count--;\n",
                indent, indent);
            break;

        case ACT_CATCH:
            if (is_tagged_union(spec)) {
                vmgen_buf_appendf(buf, "%s%s_value_t %s = vm->current_exception;\n",
                                  indent, spec->name, s->ident);
            } else {
                vmgen_buf_appendf(buf, "%s%s %s = vm->current_exception;\n",
                                  indent, vtype, s->ident);
            }
            break;

        case ACT_THROW: {
            char upper[VMGEN_MAX_NAME];
            make_upper(upper, spec->name, sizeof(upper));
            vmgen_buf_appendf(buf, "%s/* throw exception */\n", indent);
            if (is_tagged_union(spec)) {
                if (s->expr && s->expr->type == EXPR_IDENT) {
                    /* Passthrough: value is already a tagged union */
                    vmgen_buf_appendf(buf, "%svm->current_exception = %s;\n",
                                      indent, s->expr->u.ident);
                } else {
                    vmgen_buf_appendf(buf, "%svm->current_exception = %s_INT64(",
                                      indent, upper);
                    emit_expr(buf, spec, s->expr);
                    vmgen_buf_append(buf, ");\n");
                }
            } else {
                vmgen_buf_appendf(buf, "%svm->current_exception = ", indent);
                emit_expr(buf, spec, s->expr);
                vmgen_buf_append(buf, ";\n");
            }
            vmgen_buf_appendf(buf,
                "%sif (vm->exception_count > 0) {\n"
                "%s    vm->exception_count--;\n"
                "%s    vm->sp = vm->exception_frames[vm->exception_count].saved_sp;\n"
                "%s    vm->frame_count = vm->exception_frames[vm->exception_count].saved_frame_count;\n"
                "%s    vm->ip = vm->exception_frames[vm->exception_count].handler_ip;\n"
                "%s} else {\n"
                "%s    %s_vm_error(vm, %s_ERR_UNHANDLED_EXCEPTION, \"unhandled exception\");\n"
                "%s    return -1;\n"
                "%s}\n",
                indent, indent, indent, indent, indent,
                indent, indent, spec->name, upper,
                indent, indent);
            break;
        }
        }
    }
    return 0;
}

void vmgen_action_emit_inline_c(vmgen_buf_t *buf, const char *indent,
                                const char *code) {
    if (!code || !code[0]) return;
    const char *p = code;
    while (*p) {
        vmgen_buf_append(buf, indent);
        /* Find end of line */
        const char *eol = strchr(p, '\n');
        if (eol) {
            vmgen_buf_appendf(buf, "%.*s\n", (int)(eol - p), p);
            p = eol + 1;
        } else {
            vmgen_buf_append(buf, p);
            vmgen_buf_appendc(buf, '\n');
            break;
        }
    }
}

/* Emit a single action statement in a typed context (for typed_action bodies).
 * Skips pop statements (they're handled before the type switch).
 * Uses type-appropriate accessors for push expressions. */
static void emit_typed_stmt(vmgen_buf_t *buf, const char *indent,
                            const vmgen_spec_t *spec,
                            const vmgen_act_stmt_t *s, type_ctx_t tctx) {
    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    switch (s->type) {
    case ACT_POP:
        /* Pops are emitted before the type switch - skip here */
        break;

    case ACT_PUSH:
        if (s->expr && (s->expr->type == EXPR_IDENT ||
                        s->expr->type == EXPR_CONST_INDEX)) {
            /* Passthrough */
            vmgen_buf_appendf(buf, "%s%s_vm_push(vm, ", indent, spec->name);
            emit_expr_typed(buf, spec, s->expr, 0, tctx);
            vmgen_buf_append(buf, ");\n");
        } else {
            /* Arithmetic - wrap with type-appropriate constructor */
            vmgen_buf_appendf(buf, "%s%s_vm_push(vm, %s_%s(",
                              indent, spec->name, upper,
                              type_ctx_constructor(tctx));
            emit_expr_typed(buf, spec, s->expr, 1, tctx);
            vmgen_buf_append(buf, "));\n");
        }
        break;

    default:
        /* For other statement types, fall back to default emission */
        {
            vmgen_action_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.stmts[0] = *s;
            tmp.count = 1;
            vmgen_action_emit(buf, indent, spec, NULL, &tmp);
        }
        break;
    }
}

int vmgen_action_emit_typed(vmgen_buf_t *buf, const char *indent,
                            const vmgen_spec_t *spec,
                            const vmgen_instr_t *instr) {
    if (instr->typed_action_count == 0) return -1;
    if (!is_tagged_union(spec)) return -1;

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* Parse the first typed action to identify pop statements */
    vmgen_action_t first_action;
    if (vmgen_action_parse(instr->typed_actions[0], &first_action) != 0)
        return -1;

    /* Count leading pops */
    int pop_count = 0;
    for (int i = 0; i < first_action.count; i++) {
        if (first_action.stmts[i].type == ACT_POP &&
            strcmp(first_action.stmts[i].ident, "_") != 0) {
            pop_count++;
        } else {
            break;
        }
    }

    /* Emit leading pops as full value_t (shared across all paths) */
    for (int i = 0; i < pop_count; i++) {
        vmgen_buf_appendf(buf, "%s%s_value_t %s = %s_vm_pop(vm);\n",
                          indent, spec->name, first_action.stmts[i].ident,
                          spec->name);
    }

    /* Emit type switch: if/else-if chain */
    int first_branch = 1;
    char inner_indent[64];
    snprintf(inner_indent, sizeof(inner_indent), "%s    ", indent);

    for (int t = 0; t < instr->typed_action_count; t++) {
        type_ctx_t tctx;
        if (type_name_to_ctx(instr->typed_action_types[t], &tctx) != 0)
            continue;

        /* Emit type guard: check all popped operands have matching type */
        if (first_branch) {
            vmgen_buf_appendf(buf, "%sif (", indent);
            first_branch = 0;
        } else {
            vmgen_buf_appendf(buf, "%s} else if (", indent);
        }

        for (int p = 0; p < pop_count; p++) {
            if (p > 0) vmgen_buf_append(buf, " && ");
            vmgen_buf_appendf(buf, "%s.type == %s_%s",
                              first_action.stmts[p].ident, upper,
                              type_ctx_val_type(tctx));
        }
        vmgen_buf_append(buf, ") {\n");

        /* Parse and emit this branch's action body (skip leading pops) */
        vmgen_action_t branch_action;
        if (vmgen_action_parse(instr->typed_actions[t], &branch_action) == 0) {
            for (int i = pop_count; i < branch_action.count; i++) {
                emit_typed_stmt(buf, inner_indent, spec,
                                &branch_action.stmts[i], tctx);
            }
            vmgen_action_free(&branch_action);
        }
    }

    /* Emit default/fallback clause */
    if (!first_branch) {
        vmgen_buf_appendf(buf, "%s} else {\n", indent);
        if (instr->typed_action_default) {
            vmgen_action_emit_inline_c(buf, inner_indent,
                                       instr->typed_action_default);
        } else {
            vmgen_buf_appendf(buf,
                "%s    %s_vm_error(vm, %s_ERR_TYPE_MISMATCH, "
                "\"type mismatch in %s\");\n"
                "%s    return -1;\n",
                indent, spec->name, upper, instr->name, indent);
        }
        vmgen_buf_appendf(buf, "%s}\n", indent);
    }

    vmgen_action_free(&first_action);
    return 0;
}

/* Check if an expression tree contains a binary operator */
static int expr_has_binop(const vmgen_expr_t *e) {
    if (!e) return 0;
    if (e->type == EXPR_BINOP) return 1;
    return 0;
}

/* Auto-dispatch: for plain action: with BINOP, auto-generate int64+double paths */
int vmgen_action_auto_dispatch(vmgen_buf_t *buf, const char *indent,
                               const vmgen_spec_t *spec,
                               const vmgen_instr_t *instr,
                               const vmgen_action_t *action) {
    /* Pre-conditions */
    if (!is_tagged_union(spec)) return -1;
    if (!(spec->type_flags & VMGEN_TYPE_INT64) ||
        !(spec->type_flags & VMGEN_TYPE_DOUBLE)) return -1;

    /* Find a push with BINOP */
    int push_idx = -1;
    for (int i = 0; i < action->count; i++) {
        if (action->stmts[i].type == ACT_PUSH &&
            action->stmts[i].expr &&
            expr_has_binop(action->stmts[i].expr)) {
            push_idx = i;
            break;
        }
    }
    if (push_idx < 0) return -1;

    /* Count leading pops */
    int pop_count = 0;
    for (int i = 0; i < push_idx; i++) {
        if (action->stmts[i].type == ACT_POP &&
            strcmp(action->stmts[i].ident, "_") != 0) {
            pop_count++;
        } else {
            break;
        }
    }
    if (pop_count == 0) return -1;  /* Need at least one pop for type dispatch */

    char upper[VMGEN_MAX_NAME];
    make_upper(upper, spec->name, sizeof(upper));

    /* Emit leading pops as full value_t */
    for (int i = 0; i < pop_count; i++) {
        vmgen_buf_appendf(buf, "%s%s_value_t %s = %s_vm_pop(vm);\n",
                          indent, spec->name, action->stmts[i].ident,
                          spec->name);
    }

    char inner_indent[64];
    snprintf(inner_indent, sizeof(inner_indent), "%s    ", indent);

    /* INT64 branch */
    vmgen_buf_appendf(buf, "%sif (", indent);
    for (int p = 0; p < pop_count; p++) {
        if (p > 0) vmgen_buf_append(buf, " && ");
        vmgen_buf_appendf(buf, "%s.type == %s_VAL_INT64",
                          action->stmts[p].ident, upper);
    }
    vmgen_buf_append(buf, ") {\n");
    for (int i = pop_count; i < action->count; i++) {
        emit_typed_stmt(buf, inner_indent, spec, &action->stmts[i], TYPE_CTX_INT64);
    }

    /* DOUBLE branch */
    vmgen_buf_appendf(buf, "%s} else if (", indent);
    for (int p = 0; p < pop_count; p++) {
        if (p > 0) vmgen_buf_append(buf, " && ");
        vmgen_buf_appendf(buf, "%s.type == %s_VAL_DOUBLE",
                          action->stmts[p].ident, upper);
    }
    vmgen_buf_append(buf, ") {\n");
    for (int i = pop_count; i < action->count; i++) {
        emit_typed_stmt(buf, inner_indent, spec, &action->stmts[i], TYPE_CTX_DOUBLE);
    }

    /* Type error fallback */
    vmgen_buf_appendf(buf, "%s} else {\n", indent);
    vmgen_buf_appendf(buf,
        "%s    %s_vm_error(vm, %s_ERR_TYPE_MISMATCH, "
        "\"type mismatch in %s\");\n"
        "%s    return -1;\n",
        indent, spec->name, upper, instr->name, indent);
    vmgen_buf_appendf(buf, "%s}\n", indent);

    return 0;
}