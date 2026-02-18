#include "parser.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/*
 * The parser uses setjmp/longjmp purely for *local* error recovery: when a
 * production cannot continue it longjmps back to the enclosing
 * declaration/statement loop, which synchronizes and resumes.  This keeps the
 * recursive grammar functions free of error-propagation plumbing while still
 * reporting multiple errors per run.
 */

typedef struct {
    const Token *tokens;
    size_t       pos;
    Diagnostics *diag;
    jmp_buf      recover; /* target for the current sync point */
    bool         recovering;
} Parser;

/* ---- token cursor helpers -------------------------------------------- */

static const Token *peek(Parser *p)     { return &p->tokens[p->pos]; }
static const Token *previous(Parser *p) { return &p->tokens[p->pos - 1]; }
static bool is_at_end(Parser *p)        { return peek(p)->type == TOK_EOF; }

static const Token *advance(Parser *p) {
    if (!is_at_end(p)) p->pos++;
    return previous(p);
}

static bool check(Parser *p, TokenType t) { return peek(p)->type == t; }

static bool match(Parser *p, TokenType t) {
    if (check(p, t)) { advance(p); return true; }
    return false;
}

/* Report an error at the current token and jump to the recovery point. */
static void error_here(Parser *p, const char *msg) {
    const Token *t = peek(p);
    diag_error(p->diag, t->line, t->column, "%s (got '%s')",
               msg, t->type == TOK_EOF ? "end of file" : t->lexeme);
    longjmp(p->recover, 1);
}

static const Token *consume(Parser *p, TokenType t, const char *msg) {
    if (check(p, t)) return advance(p);
    error_here(p, msg);
    return NULL; /* unreachable */
}

/* Allocate a NUL-terminated copy of a token's lexeme. */
static char *dup_lexeme(const Token *t) {
    size_t n = strlen(t->lexeme);
    char *s = malloc(n + 1);
    memcpy(s, t->lexeme, n + 1);
    return s;
}

/* ---- forward declarations -------------------------------------------- */

static Expr  *parse_expression(Parser *p);
static Expr  *parse_assignment(Parser *p);
static Block  parse_block(Parser *p);
static Stmt  *parse_statement(Parser *p);

/* ---- type annotations ------------------------------------------------ */

static Type parse_type(Parser *p) {
    if (match(p, TOK_INT_TYPE))    return TYPE_INT;
    if (match(p, TOK_BOOL_TYPE))   return TYPE_BOOL;
    if (match(p, TOK_STRING_TYPE)) return TYPE_STRING;
    if (match(p, TOK_VOID_TYPE))   return TYPE_VOID;
    error_here(p, "expected a type name");
    return TYPE_ERROR; /* unreachable */
}

/* ---- expressions (precedence climbing) ------------------------------- */

static Expr *parse_primary(Parser *p) {
    const Token *t = peek(p);
    switch (t->type) {
        case TOK_INT:
            advance(p);
            return expr_int(t->int_value, t->line, t->column);
        case TOK_TRUE:
            advance(p);
            return expr_bool(true, t->line, t->column);
        case TOK_FALSE:
            advance(p);
            return expr_bool(false, t->line, t->column);
        case TOK_STRING: {
            advance(p);
            char *copy = malloc((size_t)t->string_len + 1);
            memcpy(copy, t->string_value, (size_t)t->string_len + 1);
            return expr_string(copy, t->string_len, t->line, t->column);
        }
        case TOK_LPAREN: {
            advance(p);
            Expr *e = parse_expression(p);
            consume(p, TOK_RPAREN, "expected ')' after expression");
            return e;
        }
        case TOK_IDENT: {
            advance(p);
            char *name = dup_lexeme(t);
            if (check(p, TOK_LPAREN)) {
                /* function call */
                advance(p);
                Expr **args = NULL;
                int argc = 0, cap = 0;
                if (!check(p, TOK_RPAREN)) {
                    do {
                        Expr *a = parse_expression(p);
                        if (argc == cap) {
                            cap = cap < 4 ? 4 : cap * 2;
                            args = realloc(args, (size_t)cap * sizeof(Expr *));
                        }
                        args[argc++] = a;
                    } while (match(p, TOK_COMMA));
                }
                consume(p, TOK_RPAREN, "expected ')' after arguments");
                return expr_call(name, args, argc, t->line, t->column);
            }
            return expr_var(name, t->line, t->column);
        }
        default:
            error_here(p, "expected an expression");
            return NULL; /* unreachable */
    }
}

static Expr *parse_unary(Parser *p) {
    const Token *t = peek(p);
    if (t->type == TOK_MINUS || t->type == TOK_BANG) {
        advance(p);
        Expr *operand = parse_unary(p);
        UnaryOp op = (t->type == TOK_MINUS) ? UN_NEG : UN_NOT;
        return expr_unary(op, operand, t->line, t->column);
    }
    return parse_primary(p);
}

static Expr *parse_factor(Parser *p) {
    Expr *left = parse_unary(p);
    for (;;) {
        const Token *t = peek(p);
        BinaryOp op;
        if      (t->type == TOK_STAR)    op = BIN_MUL;
        else if (t->type == TOK_SLASH)   op = BIN_DIV;
        else if (t->type == TOK_PERCENT) op = BIN_MOD;
        else break;
        advance(p);
        Expr *right = parse_unary(p);
        left = expr_binary(op, left, right, t->line, t->column);
    }
    return left;
}

static Expr *parse_term(Parser *p) {
    Expr *left = parse_factor(p);
    for (;;) {
        const Token *t = peek(p);
        BinaryOp op;
        if      (t->type == TOK_PLUS)  op = BIN_ADD;
        else if (t->type == TOK_MINUS) op = BIN_SUB;
        else break;
        advance(p);
        Expr *right = parse_factor(p);
        left = expr_binary(op, left, right, t->line, t->column);
    }
    return left;
}

static Expr *parse_comparison(Parser *p) {
    Expr *left = parse_term(p);
    for (;;) {
        const Token *t = peek(p);
        BinaryOp op;
        if      (t->type == TOK_LT) op = BIN_LT;
        else if (t->type == TOK_LE) op = BIN_LE;
        else if (t->type == TOK_GT) op = BIN_GT;
        else if (t->type == TOK_GE) op = BIN_GE;
        else break;
        advance(p);
        Expr *right = parse_term(p);
        left = expr_binary(op, left, right, t->line, t->column);
    }
    return left;
}

static Expr *parse_equality(Parser *p) {
    Expr *left = parse_comparison(p);
    for (;;) {
        const Token *t = peek(p);
        BinaryOp op;
        if      (t->type == TOK_EQ) op = BIN_EQ;
        else if (t->type == TOK_NE) op = BIN_NE;
        else break;
        advance(p);
        Expr *right = parse_comparison(p);
        left = expr_binary(op, left, right, t->line, t->column);
    }
    return left;
}

static Expr *parse_logic_and(Parser *p) {
    Expr *left = parse_equality(p);
    while (check(p, TOK_AND)) {
        const Token *t = advance(p);
        Expr *right = parse_equality(p);
        left = expr_binary(BIN_AND, left, right, t->line, t->column);
    }
    return left;
}

static Expr *parse_logic_or(Parser *p) {
    Expr *left = parse_logic_and(p);
    while (check(p, TOK_OR)) {
        const Token *t = advance(p);
        Expr *right = parse_logic_and(p);
        left = expr_binary(BIN_OR, left, right, t->line, t->column);
    }
    return left;
}

/* Assignment is right-associative and only legal with a variable target. */
static Expr *parse_assignment(Parser *p) {
    Expr *left = parse_logic_or(p);
    if (check(p, TOK_ASSIGN)) {
        const Token *eq = advance(p);
        Expr *value = parse_assignment(p);
        if (left->kind != EXPR_VAR) {
            diag_error(p->diag, eq->line, eq->column,
                       "invalid assignment target");
            ast_free_expr(left);
            ast_free_expr(value);
            longjmp(p->recover, 1);
        }
        char *name = left->as.var.name;
        left->as.var.name = NULL; /* steal the name before freeing */
        ast_free_expr(left);
        return expr_assign(name, value, eq->line, eq->column);
    }
    return left;
}

static Expr *parse_expression(Parser *p) {
    return parse_assignment(p);
}

/* ---- statements ------------------------------------------------------ */

static Stmt *parse_let(Parser *p) {
    const Token *kw = previous(p); /* the 'let' */
    const Token *name = consume(p, TOK_IDENT, "expected variable name after 'let'");
    char *vname = dup_lexeme(name);

    bool has_type = false;
    Type decl_type = TYPE_ERROR;
    if (match(p, TOK_COLON)) {
        has_type = true;
        decl_type = parse_type(p);
    }
    consume(p, TOK_ASSIGN, "expected '=' in let declaration (initializer required)");
    Expr *init = parse_expression(p);
    consume(p, TOK_SEMICOLON, "expected ';' after let declaration");
    return stmt_let(vname, has_type, decl_type, init, kw->line, kw->column);
}

static Stmt *parse_if(Parser *p) {
    const Token *kw = previous(p);
    consume(p, TOK_LPAREN, "expected '(' after 'if'");
    Expr *cond = parse_expression(p);
    consume(p, TOK_RPAREN, "expected ')' after if condition");
    consume(p, TOK_LBRACE, "expected '{' to start if body");
    Block then_blk = parse_block(p);

    bool has_else = false;
    Block else_blk;
    block_init(&else_blk);
    if (match(p, TOK_ELSE)) {
        has_else = true;
        if (match(p, TOK_IF)) {
            /* `else if` desugars to an else block holding a single if. */
            Stmt *inner = parse_if(p);
            block_push(&else_blk, inner);
        } else {
            consume(p, TOK_LBRACE, "expected '{' to start else body");
            else_blk = parse_block(p);
        }
    }
    return stmt_if(cond, then_blk, has_else, else_blk, kw->line, kw->column);
}

static Stmt *parse_while(Parser *p) {
    const Token *kw = previous(p);
    consume(p, TOK_LPAREN, "expected '(' after 'while'");
    Expr *cond = parse_expression(p);
    consume(p, TOK_RPAREN, "expected ')' after while condition");
    consume(p, TOK_LBRACE, "expected '{' to start while body");
    Block body = parse_block(p);
    return stmt_while(cond, body, kw->line, kw->column);
}

static Stmt *parse_return(Parser *p) {
    const Token *kw = previous(p);
    Expr *value = NULL;
    if (!check(p, TOK_SEMICOLON)) value = parse_expression(p);
    consume(p, TOK_SEMICOLON, "expected ';' after return");
    return stmt_return(value, kw->line, kw->column);
}

static Stmt *parse_statement(Parser *p) {
    if (match(p, TOK_LET))    return parse_let(p);
    if (match(p, TOK_IF))     return parse_if(p);
    if (match(p, TOK_WHILE))  return parse_while(p);
    if (match(p, TOK_RETURN)) return parse_return(p);
    if (check(p, TOK_LBRACE)) {
        const Token *brace = advance(p);
        Block blk = parse_block(p);
        return stmt_block(blk, brace->line, brace->column);
    }
    /* expression statement */
    const Token *t = peek(p);
    Expr *e = parse_expression(p);
    consume(p, TOK_SEMICOLON, "expected ';' after expression");
    return stmt_expr(e, t->line, t->column);
}

/* Synchronize after an error: skip tokens until a likely statement start. */
static void synchronize(Parser *p) {
    while (!is_at_end(p)) {
        if (previous(p)->type == TOK_SEMICOLON) return;
        switch (peek(p)->type) {
            case TOK_LET: case TOK_IF: case TOK_WHILE:
            case TOK_RETURN: case TOK_FN: case TOK_RBRACE:
                return;
            default:
                advance(p);
        }
    }
}

/* Parse statements until the closing '}', consuming it.  Recovers per-stmt. */
static Block parse_block(Parser *p) {
    Block blk;
    block_init(&blk);

    while (!check(p, TOK_RBRACE) && !is_at_end(p)) {
        jmp_buf saved;
        memcpy(saved, p->recover, sizeof(jmp_buf));
        if (setjmp(p->recover) == 0) {
            Stmt *s = parse_statement(p);
            block_push(&blk, s);
        } else {
            /* an error inside the statement: resync and continue */
            synchronize(p);
        }
        memcpy(p->recover, saved, sizeof(jmp_buf));
    }
    consume(p, TOK_RBRACE, "expected '}' to close block");
    return blk;
}

/* ---- declarations ---------------------------------------------------- */

static Function *parse_function(Parser *p) {
    const Token *kw = previous(p); /* 'fn' */
    const Token *name = consume(p, TOK_IDENT, "expected function name after 'fn'");
    char *fname = dup_lexeme(name);

    consume(p, TOK_LPAREN, "expected '(' after function name");
    Param *params = NULL;
    int pc = 0, cap = 0;
    if (!check(p, TOK_RPAREN)) {
        do {
            const Token *pn = consume(p, TOK_IDENT, "expected parameter name");
            consume(p, TOK_COLON, "expected ':' after parameter name");
            Type pt = parse_type(p);
            if (pc == cap) {
                cap = cap < 4 ? 4 : cap * 2;
                params = realloc(params, (size_t)cap * sizeof(Param));
            }
            params[pc].name = dup_lexeme(pn);
            params[pc].type = pt;
            pc++;
        } while (match(p, TOK_COMMA));
    }
    consume(p, TOK_RPAREN, "expected ')' after parameters");
    consume(p, TOK_COLON, "expected ':' before return type");
    Type ret = parse_type(p);
    consume(p, TOK_LBRACE, "expected '{' to start function body");
    Block body = parse_block(p);

    return function_new(fname, params, pc, ret, body, kw->line, kw->column);
}

Program *parse(const TokenList *tokens, Diagnostics *diag) {
    Parser p;
    p.tokens = tokens->tokens;
    p.pos = 0;
    p.diag = diag;
    p.recovering = false;

    Program *prog = program_new();

    while (!is_at_end(&p)) {
        if (setjmp(p.recover) == 0) {
            if (match(&p, TOK_FN)) {
                Function *fn = parse_function(&p);
                program_push(prog, fn);
            } else {
                error_here(&p, "expected a function declaration");
            }
        } else {
            synchronize(&p);
        }
    }

    if (diag_had_error(diag)) {
        ast_free_program(prog);
        return NULL;
    }
    return prog;
}
