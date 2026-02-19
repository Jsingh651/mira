#include "typecheck.h"

#include <stdlib.h>
#include <string.h>

/* ---- variable scope stack -------------------------------------------- */

typedef struct {
    char *name;   /* borrowed from the AST (not owned) */
    Type  type;
} VarEntry;

typedef struct Scope {
    VarEntry     *vars;
    int           count;
    int           capacity;
    struct Scope *parent;
} Scope;

typedef struct {
    Program     *prog;
    Diagnostics *diag;
    Scope       *scope;       /* innermost */
    Type         current_ret; /* return type of the function being checked */
} Checker;

static void scope_push(Checker *c) {
    Scope *s = calloc(1, sizeof(Scope));
    s->parent = c->scope;
    c->scope = s;
}

static void scope_pop(Checker *c) {
    Scope *s = c->scope;
    c->scope = s->parent;
    free(s->vars);
    free(s);
}

/* Declare a variable in the current scope; reports redeclaration. */
static void scope_declare(Checker *c, const char *name, Type type,
                          int line, int col) {
    Scope *s = c->scope;
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            diag_error(c->diag, line, col,
                       "redeclaration of '%s' in the same scope", name);
            return;
        }
    }
    if (s->count == s->capacity) {
        s->capacity = s->capacity < 4 ? 4 : s->capacity * 2;
        s->vars = realloc(s->vars, (size_t)s->capacity * sizeof(VarEntry));
    }
    s->vars[s->count].name = (char *)name;
    s->vars[s->count].type = type;
    s->count++;
}

/* Resolve a name through the scope chain. Returns false if undeclared. */
static bool scope_lookup(Checker *c, const char *name, Type *out) {
    for (Scope *s = c->scope; s; s = s->parent) {
        for (int i = 0; i < s->count; i++) {
            if (strcmp(s->vars[i].name, name) == 0) {
                *out = s->vars[i].type;
                return true;
            }
        }
    }
    return false;
}

static Function *find_function(Checker *c, const char *name) {
    for (int i = 0; i < c->prog->count; i++) {
        if (strcmp(c->prog->functions[i]->name, name) == 0)
            return c->prog->functions[i];
    }
    return NULL;
}

/* ---- expression checking --------------------------------------------- */

static Type check_expr(Checker *c, Expr *e);

static bool is_value_type(Type t) {
    return t == TYPE_INT || t == TYPE_BOOL || t == TYPE_STRING;
}

static Type check_unary(Checker *c, Expr *e) {
    Type t = check_expr(c, e->as.unary.operand);
    if (t == TYPE_ERROR) return TYPE_ERROR;
    if (e->as.unary.op == UN_NEG) {
        if (t != TYPE_INT) {
            diag_error(c->diag, e->line, e->column,
                       "unary '-' requires int, got %s", type_name(t));
            return TYPE_ERROR;
        }
        return TYPE_INT;
    }
    /* UN_NOT */
    if (t != TYPE_BOOL) {
        diag_error(c->diag, e->line, e->column,
                   "unary '!' requires bool, got %s", type_name(t));
        return TYPE_ERROR;
    }
    return TYPE_BOOL;
}

static Type check_binary(Checker *c, Expr *e) {
    BinaryOp op = e->as.binary.op;
    Type l = check_expr(c, e->as.binary.left);
    Type r = check_expr(c, e->as.binary.right);
    if (l == TYPE_ERROR || r == TYPE_ERROR) return TYPE_ERROR;

    switch (op) {
        case BIN_ADD:
            if (l == TYPE_INT && r == TYPE_INT) return TYPE_INT;
            if (l == TYPE_STRING && r == TYPE_STRING) return TYPE_STRING;
            diag_error(c->diag, e->line, e->column,
                       "'+' requires two ints or two strings, got %s and %s",
                       type_name(l), type_name(r));
            return TYPE_ERROR;
        case BIN_SUB: case BIN_MUL: case BIN_DIV: case BIN_MOD:
            if (l == TYPE_INT && r == TYPE_INT) return TYPE_INT;
            diag_error(c->diag, e->line, e->column,
                       "'%s' requires two ints, got %s and %s",
                       binop_symbol(op), type_name(l), type_name(r));
            return TYPE_ERROR;
        case BIN_LT: case BIN_LE: case BIN_GT: case BIN_GE:
            if (l == TYPE_INT && r == TYPE_INT) return TYPE_BOOL;
            diag_error(c->diag, e->line, e->column,
                       "'%s' requires two ints, got %s and %s",
                       binop_symbol(op), type_name(l), type_name(r));
            return TYPE_ERROR;
        case BIN_EQ: case BIN_NE:
            if (l == r && is_value_type(l)) return TYPE_BOOL;
            diag_error(c->diag, e->line, e->column,
                       "'%s' requires matching value types, got %s and %s",
                       binop_symbol(op), type_name(l), type_name(r));
            return TYPE_ERROR;
        case BIN_AND: case BIN_OR:
            if (l == TYPE_BOOL && r == TYPE_BOOL) return TYPE_BOOL;
            diag_error(c->diag, e->line, e->column,
                       "'%s' requires two bools, got %s and %s",
                       binop_symbol(op), type_name(l), type_name(r));
            return TYPE_ERROR;
    }
    return TYPE_ERROR;
}

static Type check_call(Checker *c, Expr *e) {
    const char *name = e->as.call.name;

    /* The built-in print(expr): accepts any single non-void value. */
    if (strcmp(name, "print") == 0) {
        e->as.call.is_print = true;
        if (e->as.call.arg_count != 1) {
            diag_error(c->diag, e->line, e->column,
                       "print expects exactly 1 argument, got %d",
                       e->as.call.arg_count);
            return TYPE_ERROR;
        }
        Type at = check_expr(c, e->as.call.args[0]);
        if (at == TYPE_ERROR) return TYPE_ERROR;
        if (!is_value_type(at)) {
            diag_error(c->diag, e->line, e->column,
                       "print cannot take a %s value", type_name(at));
            return TYPE_ERROR;
        }
        return TYPE_VOID;
    }

    Function *fn = find_function(c, name);
    if (!fn) {
        diag_error(c->diag, e->line, e->column,
                   "call to undeclared function '%s'", name);
        /* still check args to surface nested errors */
        for (int i = 0; i < e->as.call.arg_count; i++)
            check_expr(c, e->as.call.args[i]);
        return TYPE_ERROR;
    }

    if (e->as.call.arg_count != fn->param_count) {
        diag_error(c->diag, e->line, e->column,
                   "function '%s' expects %d argument(s), got %d",
                   name, fn->param_count, e->as.call.arg_count);
    }
    int n = e->as.call.arg_count < fn->param_count
                ? e->as.call.arg_count : fn->param_count;
    for (int i = 0; i < e->as.call.arg_count; i++) {
        Type at = check_expr(c, e->as.call.args[i]);
        if (i < n && at != TYPE_ERROR && at != fn->params[i].type) {
            diag_error(c->diag, e->as.call.args[i]->line,
                       e->as.call.args[i]->column,
                       "argument %d of '%s' expects %s, got %s",
                       i + 1, name, type_name(fn->params[i].type),
                       type_name(at));
        }
    }
    return fn->return_type;
}

static Type check_assign(Checker *c, Expr *e) {
    Type vt;
    if (!scope_lookup(c, e->as.assign.name, &vt)) {
        diag_error(c->diag, e->line, e->column,
                   "assignment to undeclared variable '%s'", e->as.assign.name);
        check_expr(c, e->as.assign.value);
        return TYPE_ERROR;
    }
    Type rt = check_expr(c, e->as.assign.value);
    if (rt == TYPE_ERROR) return TYPE_ERROR;
    if (rt != vt) {
        diag_error(c->diag, e->line, e->column,
                   "cannot assign %s to variable '%s' of type %s",
                   type_name(rt), e->as.assign.name, type_name(vt));
        return TYPE_ERROR;
    }
    return vt;
}

static Type check_expr(Checker *c, Expr *e) {
    Type t;
    switch (e->kind) {
        case EXPR_INT:    t = TYPE_INT;    break;
        case EXPR_BOOL:   t = TYPE_BOOL;   break;
        case EXPR_STRING: t = TYPE_STRING; break;
        case EXPR_VAR:
            if (!scope_lookup(c, e->as.var.name, &t)) {
                diag_error(c->diag, e->line, e->column,
                           "use of undeclared variable '%s'", e->as.var.name);
                t = TYPE_ERROR;
            }
            break;
        case EXPR_UNARY:  t = check_unary(c, e);  break;
        case EXPR_BINARY: t = check_binary(c, e); break;
        case EXPR_ASSIGN: t = check_assign(c, e); break;
        case EXPR_CALL:   t = check_call(c, e);   break;
        default:          t = TYPE_ERROR;         break;
    }
    e->type = t;
    return t;
}

/* ---- statement checking ---------------------------------------------- */

static void check_block(Checker *c, Block *b, bool new_scope);

static void check_stmt(Checker *c, Stmt *s) {
    switch (s->kind) {
        case STMT_LET: {
            Type it = check_expr(c, s->as.let.init);
            Type vt = it;
            if (s->as.let.has_decl_type) {
                vt = s->as.let.decl_type;
                if (it != TYPE_ERROR && it != vt) {
                    diag_error(c->diag, s->line, s->column,
                               "let '%s' declared %s but initialized with %s",
                               s->as.let.name, type_name(vt), type_name(it));
                }
            } else if (it == TYPE_VOID) {
                diag_error(c->diag, s->line, s->column,
                           "cannot infer type for '%s' from a void value",
                           s->as.let.name);
                vt = TYPE_ERROR;
            }
            if (vt == TYPE_VOID) {
                diag_error(c->diag, s->line, s->column,
                           "variable '%s' cannot have type void", s->as.let.name);
                vt = TYPE_ERROR;
            }
            s->as.let.type = vt;
            scope_declare(c, s->as.let.name, vt, s->line, s->column);
            break;
        }
        case STMT_EXPR:
            check_expr(c, s->as.expr);
            break;
        case STMT_IF: {
            Type ct = check_expr(c, s->as.if_stmt.cond);
            if (ct != TYPE_ERROR && ct != TYPE_BOOL)
                diag_error(c->diag, s->line, s->column,
                           "if condition must be bool, got %s", type_name(ct));
            check_block(c, &s->as.if_stmt.then_blk, true);
            if (s->as.if_stmt.has_else)
                check_block(c, &s->as.if_stmt.else_blk, true);
            break;
        }
        case STMT_WHILE: {
            Type ct = check_expr(c, s->as.while_stmt.cond);
            if (ct != TYPE_ERROR && ct != TYPE_BOOL)
                diag_error(c->diag, s->line, s->column,
                           "while condition must be bool, got %s",
                           type_name(ct));
            check_block(c, &s->as.while_stmt.body, true);
            break;
        }
        case STMT_RETURN: {
            if (s->as.ret.value == NULL) {
                if (c->current_ret != TYPE_VOID)
                    diag_error(c->diag, s->line, s->column,
                               "missing return value in function returning %s",
                               type_name(c->current_ret));
            } else {
                Type rt = check_expr(c, s->as.ret.value);
                if (c->current_ret == TYPE_VOID) {
                    diag_error(c->diag, s->line, s->column,
                               "void function cannot return a value");
                } else if (rt != TYPE_ERROR && rt != c->current_ret) {
                    diag_error(c->diag, s->line, s->column,
                               "return type mismatch: expected %s, got %s",
                               type_name(c->current_ret), type_name(rt));
                }
            }
            break;
        }
        case STMT_BLOCK:
            check_block(c, &s->as.block, true);
            break;
    }
}

static void check_block(Checker *c, Block *b, bool new_scope) {
    if (new_scope) scope_push(c);
    for (int i = 0; i < b->count; i++) check_stmt(c, b->stmts[i]);
    if (new_scope) scope_pop(c);
}

/* ---- definite-return analysis ---------------------------------------- */

static bool stmt_always_returns(const Stmt *s);

static bool block_always_returns(const Block *b) {
    for (int i = 0; i < b->count; i++)
        if (stmt_always_returns(b->stmts[i])) return true;
    return false;
}

static bool stmt_always_returns(const Stmt *s) {
    switch (s->kind) {
        case STMT_RETURN:
            return true;
        case STMT_IF:
            return s->as.if_stmt.has_else &&
                   block_always_returns(&s->as.if_stmt.then_blk) &&
                   block_always_returns(&s->as.if_stmt.else_blk);
        case STMT_BLOCK:
            return block_always_returns(&s->as.block);
        default:
            return false; /* while/let/expr cannot guarantee a return */
    }
}

/* ---- top level ------------------------------------------------------- */

static void check_function(Checker *c, Function *fn) {
    c->current_ret = fn->return_type;
    scope_push(c);
    for (int i = 0; i < fn->param_count; i++) {
        if (fn->params[i].type == TYPE_VOID)
            diag_error(c->diag, fn->line, fn->column,
                       "parameter '%s' cannot have type void", fn->params[i].name);
        scope_declare(c, fn->params[i].name, fn->params[i].type,
                      fn->line, fn->column);
    }
    /* Body shares the parameter scope (no extra scope) so a top-level let
     * collides with a parameter of the same name, which is the intent. */
    check_block(c, &fn->body, false);
    scope_pop(c);

    if (fn->return_type != TYPE_VOID && !block_always_returns(&fn->body)) {
        diag_error(c->diag, fn->line, fn->column,
                   "function '%s' returns %s but does not return on all paths",
                   fn->name, type_name(fn->return_type));
    }
}

bool typecheck(Program *program, Diagnostics *diag) {
    Checker c;
    c.prog = program;
    c.diag = diag;
    c.scope = NULL;
    c.current_ret = TYPE_VOID;

    /* Detect duplicate / reserved function names up front. */
    for (int i = 0; i < program->count; i++) {
        Function *fn = program->functions[i];
        if (strcmp(fn->name, "print") == 0)
            diag_error(diag, fn->line, fn->column,
                       "'print' is a built-in and cannot be redefined");
        for (int j = 0; j < i; j++) {
            if (strcmp(fn->name, program->functions[j]->name) == 0) {
                diag_error(diag, fn->line, fn->column,
                           "redefinition of function '%s'", fn->name);
                break;
            }
        }
    }

    for (int i = 0; i < program->count; i++)
        check_function(&c, program->functions[i]);

    /* Program entry point. */
    Function *main_fn = find_function(&c, "main");
    if (!main_fn) {
        diag_error_nopos(diag, "program has no 'main' function");
    } else {
        if (main_fn->param_count != 0)
            diag_error(diag, main_fn->line, main_fn->column,
                       "'main' must take no parameters");
        if (main_fn->return_type != TYPE_VOID)
            diag_error(diag, main_fn->line, main_fn->column,
                       "'main' must return void");
    }

    return !diag_had_error(diag);
}
