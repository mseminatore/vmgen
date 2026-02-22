#include "action_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Lexer ---- */

typedef enum {
    TOK_IDENT, TOK_OPERAND, TOK_NUMBER,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_BANG, TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET,
    TOK_ASSIGN, TOK_SEMI, TOK_EOF, TOK_ERROR,
} tok_type_t;

typedef struct {
    tok_type_t type;
    char text[64];
    long long number;
} token_t;

typedef struct {
    const char *src;
    int pos;
    token_t cur;
    char error[128];
} lexer_t;

static void skip_ws(lexer_t *l) {
    while (l->src[l->pos] && isspace((unsigned char)l->src[l->pos]))
        l->pos++;
}

static void lex_next(lexer_t *l) {
    skip_ws(l);
    char c = l->src[l->pos];

    if (c == '\0') { l->cur.type = TOK_EOF; return; }
    if (c == ';') { l->pos++; l->cur.type = TOK_SEMI; return; }
    if (c == '+') { l->pos++; l->cur.type = TOK_PLUS; return; }
    if (c == '-') { l->pos++; l->cur.type = TOK_MINUS; return; }
    if (c == '*') { l->pos++; l->cur.type = TOK_STAR; return; }
    if (c == '/') { l->pos++; l->cur.type = TOK_SLASH; return; }
    if (c == '%') { l->pos++; l->cur.type = TOK_PERCENT; return; }
    if (c == '(') { l->pos++; l->cur.type = TOK_LPAREN; return; }
    if (c == ')') { l->pos++; l->cur.type = TOK_RPAREN; return; }
    if (c == '[') { l->pos++; l->cur.type = TOK_LBRACKET; return; }
    if (c == ']') { l->pos++; l->cur.type = TOK_RBRACKET; return; }

    if (c == '!' && l->src[l->pos + 1] == '=') {
        l->pos += 2; l->cur.type = TOK_NEQ; return;
    }
    if (c == '!') { l->pos++; l->cur.type = TOK_BANG; return; }

    if (c == '=' && l->src[l->pos + 1] == '=') {
        l->pos += 2; l->cur.type = TOK_EQ; return;
    }
    if (c == '=') { l->pos++; l->cur.type = TOK_ASSIGN; return; }

    if (c == '<' && l->src[l->pos + 1] == '=') {
        l->pos += 2; l->cur.type = TOK_LE; return;
    }
    if (c == '<') { l->pos++; l->cur.type = TOK_LT; return; }

    if (c == '>' && l->src[l->pos + 1] == '=') {
        l->pos += 2; l->cur.type = TOK_GE; return;
    }
    if (c == '>') { l->pos++; l->cur.type = TOK_GT; return; }

    /* $operand reference */
    if (c == '$') {
        l->pos++;
        int start = l->pos;
        while (isalnum((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_')
            l->pos++;
        int len = l->pos - start;
        if (len == 0 || len >= 64) {
            snprintf(l->error, sizeof(l->error), "invalid operand ref at pos %d", start);
            l->cur.type = TOK_ERROR;
            return;
        }
        memcpy(l->cur.text, l->src + start, (size_t)len);
        l->cur.text[len] = '\0';
        l->cur.type = TOK_OPERAND;
        return;
    }

    /* number */
    if (isdigit((unsigned char)c)) {
        long long val = 0;
        while (isdigit((unsigned char)l->src[l->pos])) {
            val = val * 10 + (l->src[l->pos] - '0');
            l->pos++;
        }
        l->cur.number = val;
        l->cur.type = TOK_NUMBER;
        return;
    }

    /* identifier / keyword */
    if (isalpha((unsigned char)c) || c == '_') {
        int start = l->pos;
        while (isalnum((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_')
            l->pos++;
        int len = l->pos - start;
        if (len >= 64) len = 63;
        memcpy(l->cur.text, l->src + start, (size_t)len);
        l->cur.text[len] = '\0';
        l->cur.type = TOK_IDENT;
        return;
    }

    snprintf(l->error, sizeof(l->error), "unexpected char '%c' at pos %d", c, l->pos);
    l->cur.type = TOK_ERROR;
}

static void lex_init(lexer_t *l, const char *src) {
    l->src = src;
    l->pos = 0;
    l->error[0] = '\0';
    lex_next(l);
}

static int lex_match(lexer_t *l, tok_type_t type) {
    if (l->cur.type == type) { lex_next(l); return 1; }
    return 0;
}

/* ---- Expression parser (recursive descent, Pratt-style precedence) ---- */

static vmgen_expr_t *alloc_expr(vmgen_expr_type_t type) {
    vmgen_expr_t *e = (vmgen_expr_t *)calloc(1, sizeof(vmgen_expr_t));
    if (e) e->type = type;
    return e;
}

void vmgen_expr_free(vmgen_expr_t *e) {
    if (!e) return;
    switch (e->type) {
    case EXPR_BINOP:
        vmgen_expr_free(e->u.binop.left);
        vmgen_expr_free(e->u.binop.right);
        break;
    case EXPR_UNARY:
        vmgen_expr_free(e->u.unary.operand);
        break;
    case EXPR_CONST_INDEX:
        vmgen_expr_free(e->u.index);
        break;
    default:
        break;
    }
    free(e);
}

/* Forward declarations */
static vmgen_expr_t *parse_expr(lexer_t *l);
static vmgen_expr_t *parse_comparison(lexer_t *l);
static vmgen_expr_t *parse_additive(lexer_t *l);
static vmgen_expr_t *parse_multiplicative(lexer_t *l);
static vmgen_expr_t *parse_unary(lexer_t *l);
static vmgen_expr_t *parse_primary(lexer_t *l);

static vmgen_expr_t *parse_expr(lexer_t *l) {
    return parse_comparison(l);
}

static vmgen_expr_t *parse_comparison(lexer_t *l) {
    vmgen_expr_t *left = parse_additive(l);
    if (!left) return NULL;

    while (l->cur.type == TOK_EQ || l->cur.type == TOK_NEQ ||
           l->cur.type == TOK_LT || l->cur.type == TOK_GT ||
           l->cur.type == TOK_LE || l->cur.type == TOK_GE) {
        vmgen_op_t op;
        switch (l->cur.type) {
        case TOK_EQ:  op = OP_EQ;  break;
        case TOK_NEQ: op = OP_NEQ; break;
        case TOK_LT:  op = OP_LT;  break;
        case TOK_GT:  op = OP_GT;  break;
        case TOK_LE:  op = OP_LE;  break;
        default:      op = OP_GE;  break;
        }
        lex_next(l);
        vmgen_expr_t *right = parse_additive(l);
        if (!right) { vmgen_expr_free(left); return NULL; }
        vmgen_expr_t *node = alloc_expr(EXPR_BINOP);
        node->u.binop.op = op;
        node->u.binop.left = left;
        node->u.binop.right = right;
        left = node;
    }
    return left;
}

static vmgen_expr_t *parse_additive(lexer_t *l) {
    vmgen_expr_t *left = parse_multiplicative(l);
    if (!left) return NULL;

    while (l->cur.type == TOK_PLUS || l->cur.type == TOK_MINUS) {
        vmgen_op_t op = (l->cur.type == TOK_PLUS) ? OP_ADD : OP_SUB;
        lex_next(l);
        vmgen_expr_t *right = parse_multiplicative(l);
        if (!right) { vmgen_expr_free(left); return NULL; }
        vmgen_expr_t *node = alloc_expr(EXPR_BINOP);
        node->u.binop.op = op;
        node->u.binop.left = left;
        node->u.binop.right = right;
        left = node;
    }
    return left;
}

static vmgen_expr_t *parse_multiplicative(lexer_t *l) {
    vmgen_expr_t *left = parse_unary(l);
    if (!left) return NULL;

    while (l->cur.type == TOK_STAR || l->cur.type == TOK_SLASH ||
           l->cur.type == TOK_PERCENT) {
        vmgen_op_t op;
        switch (l->cur.type) {
        case TOK_STAR:    op = OP_MUL; break;
        case TOK_SLASH:   op = OP_DIV; break;
        default:          op = OP_MOD; break;
        }
        lex_next(l);
        vmgen_expr_t *right = parse_unary(l);
        if (!right) { vmgen_expr_free(left); return NULL; }
        vmgen_expr_t *node = alloc_expr(EXPR_BINOP);
        node->u.binop.op = op;
        node->u.binop.left = left;
        node->u.binop.right = right;
        left = node;
    }
    return left;
}

static vmgen_expr_t *parse_unary(lexer_t *l) {
    if (l->cur.type == TOK_MINUS) {
        lex_next(l);
        vmgen_expr_t *operand = parse_unary(l);
        if (!operand) return NULL;
        vmgen_expr_t *node = alloc_expr(EXPR_UNARY);
        node->u.unary.op = OP_NEG;
        node->u.unary.operand = operand;
        return node;
    }
    if (l->cur.type == TOK_BANG) {
        lex_next(l);
        vmgen_expr_t *operand = parse_unary(l);
        if (!operand) return NULL;
        vmgen_expr_t *node = alloc_expr(EXPR_UNARY);
        node->u.unary.op = OP_NOT;
        node->u.unary.operand = operand;
        return node;
    }
    return parse_primary(l);
}

static vmgen_expr_t *parse_primary(lexer_t *l) {
    if (l->cur.type == TOK_NUMBER) {
        vmgen_expr_t *node = alloc_expr(EXPR_NUMBER);
        node->u.number = l->cur.number;
        lex_next(l);
        return node;
    }

    if (l->cur.type == TOK_OPERAND) {
        vmgen_expr_t *node = alloc_expr(EXPR_OPERAND);
        snprintf(node->u.ident, sizeof(node->u.ident), "%s", l->cur.text);
        lex_next(l);
        return node;
    }

    if (l->cur.type == TOK_IDENT) {
        /* Check for constants[expr] */
        if (strcmp(l->cur.text, "constants") == 0) {
            lex_next(l);
            if (l->cur.type != TOK_LBRACKET) {
                snprintf(l->error, sizeof(l->error), "expected '[' after 'constants'");
                return NULL;
            }
            lex_next(l);
            vmgen_expr_t *idx = parse_expr(l);
            if (!idx) return NULL;
            if (l->cur.type != TOK_RBRACKET) {
                vmgen_expr_free(idx);
                snprintf(l->error, sizeof(l->error), "expected ']' after constants index");
                return NULL;
            }
            lex_next(l);
            vmgen_expr_t *node = alloc_expr(EXPR_CONST_INDEX);
            node->u.index = idx;
            return node;
        }

        vmgen_expr_t *node = alloc_expr(EXPR_IDENT);
        snprintf(node->u.ident, sizeof(node->u.ident), "%s", l->cur.text);
        lex_next(l);
        return node;
    }

    if (l->cur.type == TOK_LPAREN) {
        lex_next(l);
        vmgen_expr_t *node = parse_expr(l);
        if (!node) return NULL;
        if (l->cur.type != TOK_RPAREN) {
            vmgen_expr_free(node);
            snprintf(l->error, sizeof(l->error), "expected ')'");
            return NULL;
        }
        lex_next(l);
        return node;
    }

    snprintf(l->error, sizeof(l->error), "unexpected token at pos %d", l->pos);
    return NULL;
}

/* ---- Statement parser ---- */

static int expect_ident(lexer_t *l, char *out, int max) {
    if (l->cur.type != TOK_IDENT) {
        snprintf(l->error, sizeof(l->error), "expected identifier at pos %d", l->pos);
        return 0;
    }
    snprintf(out, (size_t)max, "%s", l->cur.text);
    lex_next(l);
    return 1;
}

static int parse_statement(lexer_t *l, vmgen_act_stmt_t *stmt) {
    memset(stmt, 0, sizeof(*stmt));

    if (l->cur.type != TOK_IDENT) {
        snprintf(l->error, sizeof(l->error), "expected keyword at pos %d", l->pos);
        return -1;
    }

    if (strcmp(l->cur.text, "pop") == 0) {
        lex_next(l);
        stmt->type = ACT_POP;
        if (!expect_ident(l, stmt->ident, 64)) return -1;
        return 0;
    }

    if (strcmp(l->cur.text, "push") == 0) {
        lex_next(l);
        stmt->type = ACT_PUSH;
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    if (strcmp(l->cur.text, "peek") == 0) {
        lex_next(l);
        stmt->type = ACT_PEEK;
        if (!expect_ident(l, stmt->ident, 64)) return -1;
        return 0;
    }

    if (strcmp(l->cur.text, "jump") == 0) {
        lex_next(l);
        stmt->type = ACT_JUMP;
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    if (strcmp(l->cur.text, "jump_if_zero") == 0) {
        lex_next(l);
        stmt->type = ACT_JUMP_IF_ZERO;
        if (!expect_ident(l, stmt->ident, 64)) return -1;
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    if (strcmp(l->cur.text, "jump_if_nonzero") == 0) {
        lex_next(l);
        stmt->type = ACT_JUMP_IF_NONZERO;
        if (!expect_ident(l, stmt->ident, 64)) return -1;
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    if (strcmp(l->cur.text, "halt") == 0) {
        lex_next(l);
        stmt->type = ACT_HALT;
        return 0;
    }

    if (strcmp(l->cur.text, "return") == 0) {
        lex_next(l);
        stmt->type = ACT_RETURN;
        return 0;
    }

    if (strcmp(l->cur.text, "call") == 0) {
        lex_next(l);
        stmt->type = ACT_CALL;
        stmt->expr = parse_expr(l);
        if (!stmt->expr) return -1;
        stmt->expr2 = parse_expr(l);
        return stmt->expr2 ? 0 : -1;
    }

    if (strcmp(l->cur.text, "set") == 0) {
        lex_next(l);
        stmt->type = ACT_SET;
        if (!expect_ident(l, stmt->ident, 64)) return -1;
        if (!lex_match(l, TOK_ASSIGN)) {
            snprintf(l->error, sizeof(l->error), "expected '=' in set statement");
            return -1;
        }
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    if (strcmp(l->cur.text, "try_begin") == 0) {
        lex_next(l);
        stmt->type = ACT_TRY_BEGIN;
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    if (strcmp(l->cur.text, "try_end") == 0) {
        lex_next(l);
        stmt->type = ACT_TRY_END;
        return 0;
    }

    if (strcmp(l->cur.text, "catch") == 0) {
        lex_next(l);
        stmt->type = ACT_CATCH;
        if (!expect_ident(l, stmt->ident, 64)) return -1;
        return 0;
    }

    if (strcmp(l->cur.text, "throw") == 0) {
        lex_next(l);
        stmt->type = ACT_THROW;
        stmt->expr = parse_expr(l);
        return stmt->expr ? 0 : -1;
    }

    snprintf(l->error, sizeof(l->error), "unknown keyword '%s'", l->cur.text);
    return -1;
}

/* ---- Public API ---- */

int vmgen_action_parse(const char *input, vmgen_action_t *out) {
    memset(out, 0, sizeof(*out));
    if (!input || !input[0]) return 0;

    lexer_t lex;
    lex_init(&lex, input);

    while (lex.cur.type != TOK_EOF && lex.cur.type != TOK_ERROR) {
        if (out->count >= VMGEN_MAX_STMTS) {
            snprintf(out->error, sizeof(out->error), "too many statements (max %d)", VMGEN_MAX_STMTS);
            return -1;
        }
        if (parse_statement(&lex, &out->stmts[out->count]) != 0) {
            snprintf(out->error, sizeof(out->error), "%s", lex.error);
            return -1;
        }
        out->count++;
        /* Skip optional semicolons between statements */
        while (lex.cur.type == TOK_SEMI) lex_next(&lex);
    }

    if (lex.cur.type == TOK_ERROR) {
        snprintf(out->error, sizeof(out->error), "%s", lex.error);
        return -1;
    }

    return 0;
}

void vmgen_action_free(vmgen_action_t *action) {
    for (int i = 0; i < action->count; i++) {
        vmgen_expr_free(action->stmts[i].expr);
        vmgen_expr_free(action->stmts[i].expr2);
        action->stmts[i].expr = NULL;
        action->stmts[i].expr2 = NULL;
    }
    action->count = 0;
}

int vmgen_action_stack_effect(const vmgen_action_t *action) {
    int effect = 0;
    for (int i = 0; i < action->count; i++) {
        switch (action->stmts[i].type) {
        case ACT_POP:  effect--; break;
        case ACT_PUSH: effect++; break;
        case ACT_PEEK: break;
        default: break;
        }
    }
    return effect;
}
