#ifndef VMGEN_ACTION_PARSER_H
#define VMGEN_ACTION_PARSER_H

#include <stdbool.h>

/* AST node types for the action DSL */
typedef enum {
    ACT_POP,            /* pop <ident> */
    ACT_PUSH,           /* push <expr> */
    ACT_PEEK,           /* peek <ident> */
    ACT_JUMP,           /* jump <expr> */
    ACT_JUMP_IF_ZERO,   /* jump_if_zero <ident> <expr> */
    ACT_JUMP_IF_NONZERO,/* jump_if_nonzero <ident> <expr> */
    ACT_HALT,           /* halt */
    ACT_RETURN,         /* return */
    ACT_CALL,           /* call <expr> <expr> */
    ACT_SET,            /* set <ident> = <expr> */
} vmgen_act_type_t;

/* Expression node types */
typedef enum {
    EXPR_IDENT,         /* bare identifier (DSL variable) */
    EXPR_OPERAND,       /* $name (operand reference) */
    EXPR_NUMBER,        /* integer literal */
    EXPR_BINOP,         /* a op b */
    EXPR_UNARY,         /* -a or !a */
    EXPR_CONST_INDEX,   /* constants[expr] */
} vmgen_expr_type_t;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_NEG, OP_NOT,
} vmgen_op_t;

/* Expression AST node */
typedef struct vmgen_expr {
    vmgen_expr_type_t type;
    union {
        char ident[64];             /* EXPR_IDENT, EXPR_OPERAND */
        long long number;           /* EXPR_NUMBER */
        struct {                    /* EXPR_BINOP */
            vmgen_op_t op;
            struct vmgen_expr *left;
            struct vmgen_expr *right;
        } binop;
        struct {                    /* EXPR_UNARY */
            vmgen_op_t op;
            struct vmgen_expr *operand;
        } unary;
        struct vmgen_expr *index;   /* EXPR_CONST_INDEX */
    } u;
} vmgen_expr_t;

/* Action statement node */
typedef struct {
    vmgen_act_type_t type;
    char ident[64];                 /* for pop/peek/set/jump_if_* */
    vmgen_expr_t *expr;             /* primary expression */
    vmgen_expr_t *expr2;            /* second expr (call arity, jump offset) */
} vmgen_act_stmt_t;

/* Parsed action: array of statements */
#define VMGEN_MAX_STMTS 32

typedef struct {
    vmgen_act_stmt_t stmts[VMGEN_MAX_STMTS];
    int count;
    char error[128];
} vmgen_action_t;

/* Parse an action DSL string. Returns 0 on success, -1 on error. */
int vmgen_action_parse(const char *input, vmgen_action_t *out);

/* Free expression trees in an action. */
void vmgen_action_free(vmgen_action_t *action);

/* Compute net stack effect of a parsed action (+push, -pop). */
int vmgen_action_stack_effect(const vmgen_action_t *action);

#endif /* VMGEN_ACTION_PARSER_H */
