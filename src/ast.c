#include "ast.h"

#include <stdlib.h>

const char *type_name(Type t) {
    switch (t) {
        case TYPE_INT:    return "int";
        case TYPE_BOOL:   return "bool";
        case TYPE_STRING: return "string";
        case TYPE_VOID:   return "void";
        case TYPE_ERROR:  return "<error>";
    }
    return "?";
}

const char *binop_symbol(BinaryOp op) {
    switch (op) {
        case BIN_ADD: return "+";  case BIN_SUB: return "-";
        case BIN_MUL: return "*";  case BIN_DIV: return "/";
        case BIN_MOD: return "%";  case BIN_EQ:  return "==";
        case BIN_NE:  return "!="; case BIN_LT:  return "<";
        case BIN_LE:  return "<="; case BIN_GT:  return ">";
        case BIN_GE:  return ">="; case BIN_AND: return "&&";
        case BIN_OR:  return "||";
    }
    return "?";
}

const char *unop_symbol(UnaryOp op) {
    return op == UN_NEG ? "-" : "!";
}

/* ---- blocks ---------------------------------------------------------- */

void block_init(Block *b) {
    b->stmts = NULL;
    b->count = 0;
    b->capacity = 0;
}

void block_push(Block *b, Stmt *s) {
    if (b->count == b->capacity) {
        b->capacity = b->capacity < 4 ? 4 : b->capacity * 2;
        b->stmts = realloc(b->stmts, (size_t)b->capacity * sizeof(Stmt *));
    }
    b->stmts[b->count++] = s;
}

void block_free(Block *b) {
    for (int i = 0; i < b->count; i++) ast_free_stmt(b->stmts[i]);
    free(b->stmts);
    b->stmts = NULL;
    b->count = 0;
    b->capacity = 0;
}

/* ---- expression constructors ----------------------------------------- */

static Expr *new_expr(ExprKind kind, int line, int col) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = kind;
    e->type = TYPE_ERROR; /* until the checker runs */
    e->line = line;
    e->column = col;
    return e;
}

Expr *expr_int(long long v, int line, int col) {
    Expr *e = new_expr(EXPR_INT, line, col);
    e->as.int_val = v;
    return e;
}

Expr *expr_bool(bool v, int line, int col) {
    Expr *e = new_expr(EXPR_BOOL, line, col);
    e->as.bool_val = v;
    return e;
}

Expr *expr_string(char *value, int len, int line, int col) {
    Expr *e = new_expr(EXPR_STRING, line, col);
    e->as.str.value = value;
    e->as.str.len = len;
    return e;
}

Expr *expr_var(char *name, int line, int col) {
    Expr *e = new_expr(EXPR_VAR, line, col);
    e->as.var.name = name;
    e->as.var.slot = -1;
    return e;
}

Expr *expr_unary(UnaryOp op, Expr *operand, int line, int col) {
    Expr *e = new_expr(EXPR_UNARY, line, col);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr *expr_binary(BinaryOp op, Expr *l, Expr *r, int line, int col) {
    Expr *e = new_expr(EXPR_BINARY, line, col);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

Expr *expr_assign(char *name, Expr *value, int line, int col) {
    Expr *e = new_expr(EXPR_ASSIGN, line, col);
    e->as.assign.name = name;
    e->as.assign.slot = -1;
    e->as.assign.value = value;
    return e;
}

Expr *expr_call(char *name, Expr **args, int argc, int line, int col) {
    Expr *e = new_expr(EXPR_CALL, line, col);
    e->as.call.name = name;
    e->as.call.args = args;
    e->as.call.arg_count = argc;
    e->as.call.is_print = false;
    return e;
}

/* ---- statement constructors ------------------------------------------ */

static Stmt *new_stmt(StmtKind kind, int line, int col) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    s->column = col;
    return s;
}

Stmt *stmt_let(char *name, bool has_type, Type t, Expr *init, int line, int col) {
    Stmt *s = new_stmt(STMT_LET, line, col);
    s->as.let.name = name;
    s->as.let.has_decl_type = has_type;
    s->as.let.decl_type = t;
    s->as.let.type = TYPE_ERROR;
    s->as.let.init = init;
    s->as.let.slot = -1;
    s->as.let.used = true; /* assume used until DCE proves otherwise */
    return s;
}

Stmt *stmt_expr(Expr *e, int line, int col) {
    Stmt *s = new_stmt(STMT_EXPR, line, col);
    s->as.expr = e;
    return s;
}

Stmt *stmt_if(Expr *cond, Block then_blk, bool has_else, Block else_blk,
              int line, int col) {
    Stmt *s = new_stmt(STMT_IF, line, col);
    s->as.if_stmt.cond = cond;
    s->as.if_stmt.then_blk = then_blk;
    s->as.if_stmt.has_else = has_else;
    s->as.if_stmt.else_blk = else_blk;
    return s;
}

Stmt *stmt_while(Expr *cond, Block body, int line, int col) {
    Stmt *s = new_stmt(STMT_WHILE, line, col);
    s->as.while_stmt.cond = cond;
    s->as.while_stmt.body = body;
    return s;
}

Stmt *stmt_return(Expr *value, int line, int col) {
    Stmt *s = new_stmt(STMT_RETURN, line, col);
    s->as.ret.value = value;
    return s;
}

Stmt *stmt_block(Block blk, int line, int col) {
    Stmt *s = new_stmt(STMT_BLOCK, line, col);
    s->as.block = blk;
    return s;
}

/* ---- program / function ---------------------------------------------- */

Program *program_new(void) {
    Program *p = calloc(1, sizeof(Program));
    return p;
}

void program_push(Program *p, Function *fn) {
    if (p->count == p->capacity) {
        p->capacity = p->capacity < 4 ? 4 : p->capacity * 2;
        p->functions = realloc(p->functions,
                               (size_t)p->capacity * sizeof(Function *));
    }
    p->functions[p->count++] = fn;
}

Function *function_new(char *name, Param *params, int pc, Type ret,
                       Block body, int line, int col) {
    Function *fn = calloc(1, sizeof(Function));
    fn->name = name;
    fn->params = params;
    fn->param_count = pc;
    fn->return_type = ret;
    fn->body = body;
    fn->line = line;
    fn->column = col;
    fn->num_locals = 0;
    return fn;
}

/* ---- teardown -------------------------------------------------------- */

void ast_free_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_STRING: free(e->as.str.value); break;
        case EXPR_VAR:    free(e->as.var.name);  break;
        case EXPR_UNARY:  ast_free_expr(e->as.unary.operand); break;
        case EXPR_BINARY:
            ast_free_expr(e->as.binary.left);
            ast_free_expr(e->as.binary.right);
            break;
        case EXPR_ASSIGN:
            free(e->as.assign.name);
            ast_free_expr(e->as.assign.value);
            break;
        case EXPR_CALL:
            free(e->as.call.name);
            for (int i = 0; i < e->as.call.arg_count; i++)
                ast_free_expr(e->as.call.args[i]);
            free(e->as.call.args);
            break;
        case EXPR_INT:
        case EXPR_BOOL:
            break;
    }
    free(e);
}

void ast_free_stmt(Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_LET:
            free(s->as.let.name);
            ast_free_expr(s->as.let.init);
            break;
        case STMT_EXPR:
            ast_free_expr(s->as.expr);
            break;
        case STMT_IF:
            ast_free_expr(s->as.if_stmt.cond);
            block_free(&s->as.if_stmt.then_blk);
            if (s->as.if_stmt.has_else) block_free(&s->as.if_stmt.else_blk);
            break;
        case STMT_WHILE:
            ast_free_expr(s->as.while_stmt.cond);
            block_free(&s->as.while_stmt.body);
            break;
        case STMT_RETURN:
            ast_free_expr(s->as.ret.value);
            break;
        case STMT_BLOCK:
            block_free(&s->as.block);
            break;
    }
    free(s);
}

void ast_free_function(Function *fn) {
    if (!fn) return;
    free(fn->name);
    for (int i = 0; i < fn->param_count; i++) free(fn->params[i].name);
    free(fn->params);
    block_free(&fn->body);
    free(fn);
}

void ast_free_program(Program *p) {
    if (!p) return;
    for (int i = 0; i < p->count; i++) ast_free_function(p->functions[i]);
    free(p->functions);
    free(p);
}
