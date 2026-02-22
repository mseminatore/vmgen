#include "action_emit.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Check if spec uses tagged union values */
static int is_tagged_union(const vmgen_spec_t *spec) {
    return spec->stack.value_type == VMGEN_VALUE_TAGGED_UNION;
}

/* Emit a C expression from an expression AST node.
 * extract_scalar: when 1, EXPR_IDENT appends .as.i64 and EXPR_CONST_INDEX
 * appends .as.i64 (for use in arithmetic). When 0, values are emitted as
 * full tagged unions (for passthrough push/throw). */
static void emit_expr_ex(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                         const vmgen_expr_t *e, int extract_scalar) {
    if (!e) return;

    switch (e->type) {
    case EXPR_NUMBER:
        vmgen_buf_appendf(buf, "%lld", e->u.number);
        break;

    case EXPR_IDENT:
        vmgen_buf_append(buf, e->u.ident);
        if (extract_scalar && is_tagged_union(spec))
            vmgen_buf_append(buf, ".as.i64");
        break;

    case EXPR_OPERAND:
        vmgen_buf_append(buf, e->u.ident);
        break;

    case EXPR_CONST_INDEX:
        vmgen_buf_appendf(buf, "vm->constants.values[");
        emit_expr_ex(buf, spec, e->u.index, 1);
        vmgen_buf_append(buf, "]");
        if (extract_scalar && is_tagged_union(spec))
            vmgen_buf_append(buf, ".as.i64");
        break;

    case EXPR_BINOP: {
        const char *op_str = "+";
        switch (e->u.binop.op) {
        case OP_ADD: op_str = "+";  break;
        case OP_SUB: op_str = "-";  break;
        case OP_MUL: op_str = "*";  break;
        case OP_DIV: op_str = "/";  break;
        case OP_MOD: op_str = "%%"; break;
        case OP_EQ:  op_str = "=="; break;
        case OP_NEQ: op_str = "!="; break;
        case OP_LT:  op_str = "<";  break;
        case OP_GT:  op_str = ">";  break;
        case OP_LE:  op_str = "<="; break;
        case OP_GE:  op_str = ">="; break;
        default: break;
        }
        vmgen_buf_append(buf, "(");
        emit_expr_ex(buf, spec, e->u.binop.left, 1);
        vmgen_buf_appendf(buf, " %s ", op_str);
        emit_expr_ex(buf, spec, e->u.binop.right, 1);
        vmgen_buf_append(buf, ")");
        break;
    }

    case EXPR_UNARY:
        if (e->u.unary.op == OP_NEG) {
            vmgen_buf_append(buf, "(-(");
            emit_expr_ex(buf, spec, e->u.unary.operand, 1);
            vmgen_buf_append(buf, "))");
        } else {
            vmgen_buf_append(buf, "(!(");
            emit_expr_ex(buf, spec, e->u.unary.operand, 1);
            vmgen_buf_append(buf, "))");
        }
        break;
    }
}

/* Default: extract scalar for arithmetic contexts */
static void emit_expr(vmgen_buf_t *buf, const vmgen_spec_t *spec,
                      const vmgen_expr_t *e) {
    emit_expr_ex(buf, spec, e, 1);
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
