#include "optimize.h"

#include <stdlib.h>
#include <string.h>

/* The optimizer threads a single `changed` flag so the driver can loop until a
 * full pass makes no further progress (the fixed point). */

/* ---- helpers --------------------------------------------------------- */

static bool is_int_lit(const Expr *e)    { return e->kind == EXPR_INT; }
static bool is_bool_lit(const Expr *e)   { return e->kind == EXPR_BOOL; }
static bool is_string_lit(const Expr *e) { return e->kind == EXPR_STRING; }

/* An expression is pure if evaluating it cannot have observable effects. */
static bool expr_is_pure(const Expr *e) {
    switch (e->kind) {
        case EXPR_INT:
        case EXPR_BOOL:
        case EXPR_STRING:
        case EXPR_VAR:
            return true;
        case EXPR_UNARY:
            return expr_is_pure(e->as.unary.operand);
        case EXPR_BINARY:
            return expr_is_pure(e->as.binary.left) &&
                   expr_is_pure(e->as.binary.right);
        case EXPR_ASSIGN: /* writes a variable */
        case EXPR_CALL:   /* may print / recurse / trap */
            return false;
    }
    return false;
}

/* Replace *ep with a fresh literal, freeing the old subtree. */
static void replace_with_int(Expr **ep, long long v) {
    Expr *old = *ep;
    *ep = expr_int(v, old->line, old->column);
    (*ep)->type = TYPE_INT;
    ast_free_expr(old);
}

static void replace_with_bool(Expr **ep, bool v) {
    Expr *old = *ep;
    *ep = expr_bool(v, old->line, old->column);
    (*ep)->type = TYPE_BOOL;
    ast_free_expr(old);
}

/* Replace *ep with the subtree `repl`, detached from `old`, then free old. */
static void replace_with_child(Expr **ep, Expr *repl) {
    Expr *old = *ep;
    *ep = repl;
    /* `repl` is one of old's children; null it out so the free below skips it. */
    if (old->kind == EXPR_BINARY) {
        if (old->as.binary.left == repl)  old->as.binary.left = NULL;
        if (old->as.binary.right == repl) old->as.binary.right = NULL;
    } else if (old->kind == EXPR_UNARY) {
        if (old->as.unary.operand == repl) old->as.unary.operand = NULL;
    }
    ast_free_expr(old);
}

/* ---- constant folding ------------------------------------------------ */

static void fold_expr(Expr **ep, bool *changed);

static void fold_unary(Expr **ep, bool *changed) {
    Expr *e = *ep;
    fold_expr(&e->as.unary.operand, changed);
    Expr *o = e->as.unary.operand;
    if (e->as.unary.op == UN_NEG && is_int_lit(o)) {
        replace_with_int(ep, -o->as.int_val);
        *changed = true;
    } else if (e->as.unary.op == UN_NOT && is_bool_lit(o)) {
        replace_with_bool(ep, !o->as.bool_val);
        *changed = true;
    }
}

static void fold_binary(Expr **ep, bool *changed) {
    Expr *e = *ep;
    BinaryOp op = e->as.binary.op;

    /* Short-circuit operators fold off the left operand only, so that any
     * side effects in a still-live right operand are preserved. */
    if (op == BIN_AND || op == BIN_OR) {
        fold_expr(&e->as.binary.left, changed);
        fold_expr(&e->as.binary.right, changed);
        Expr *l = e->as.binary.left;
        if (is_bool_lit(l)) {
            if (op == BIN_AND) {
                if (!l->as.bool_val) { replace_with_bool(ep, false); }
                else { replace_with_child(ep, e->as.binary.right); }
            } else { /* OR */
                if (l->as.bool_val) { replace_with_bool(ep, true); }
                else { replace_with_child(ep, e->as.binary.right); }
            }
            *changed = true;
        }
        return;
    }

    fold_expr(&e->as.binary.left, changed);
    fold_expr(&e->as.binary.right, changed);
    Expr *l = e->as.binary.left;
    Expr *r = e->as.binary.right;

    /* String concatenation of two literals. */
    if (op == BIN_ADD && is_string_lit(l) && is_string_lit(r)) {
        int len = l->as.str.len + r->as.str.len;
        char *buf = malloc((size_t)len + 1);
        memcpy(buf, l->as.str.value, (size_t)l->as.str.len);
        memcpy(buf + l->as.str.len, r->as.str.value, (size_t)r->as.str.len);
        buf[len] = '\0';
        Expr *old = *ep;
        *ep = expr_string(buf, len, old->line, old->column);
        (*ep)->type = TYPE_STRING;
        ast_free_expr(old);
        *changed = true;
        return;
    }

    /* Integer arithmetic / comparison. */
    if (is_int_lit(l) && is_int_lit(r)) {
        long long a = l->as.int_val, b = r->as.int_val;
        switch (op) {
            case BIN_ADD: replace_with_int(ep, a + b); *changed = true; break;
            case BIN_SUB: replace_with_int(ep, a - b); *changed = true; break;
            case BIN_MUL: replace_with_int(ep, a * b); *changed = true; break;
            case BIN_DIV:
                if (b != 0) { replace_with_int(ep, a / b); *changed = true; }
                /* divide by zero: leave for the VM to trap at runtime */
                break;
            case BIN_MOD:
                if (b != 0) { replace_with_int(ep, a % b); *changed = true; }
                break;
            case BIN_LT: replace_with_bool(ep, a <  b); *changed = true; break;
            case BIN_LE: replace_with_bool(ep, a <= b); *changed = true; break;
            case BIN_GT: replace_with_bool(ep, a >  b); *changed = true; break;
            case BIN_GE: replace_with_bool(ep, a >= b); *changed = true; break;
            case BIN_EQ: replace_with_bool(ep, a == b); *changed = true; break;
            case BIN_NE: replace_with_bool(ep, a != b); *changed = true; break;
            default: break;
        }
        return;
    }

    /* bool == bool / bool != bool */
    if ((op == BIN_EQ || op == BIN_NE) && is_bool_lit(l) && is_bool_lit(r)) {
        bool eq = (l->as.bool_val == r->as.bool_val);
        replace_with_bool(ep, op == BIN_EQ ? eq : !eq);
        *changed = true;
        return;
    }

    /* string == string / string != string */
    if ((op == BIN_EQ || op == BIN_NE) && is_string_lit(l) && is_string_lit(r)) {
        bool eq = (l->as.str.len == r->as.str.len) &&
                  memcmp(l->as.str.value, r->as.str.value,
                         (size_t)l->as.str.len) == 0;
        replace_with_bool(ep, op == BIN_EQ ? eq : !eq);
        *changed = true;
        return;
    }
}

static void fold_expr(Expr **ep, bool *changed) {
    Expr *e = *ep;
    switch (e->kind) {
        case EXPR_UNARY:  fold_unary(ep, changed);  break;
        case EXPR_BINARY: fold_binary(ep, changed); break;
        case EXPR_ASSIGN: fold_expr(&e->as.assign.value, changed); break;
        case EXPR_CALL:
            for (int i = 0; i < e->as.call.arg_count; i++)
                fold_expr(&e->as.call.args[i], changed);
            break;
        default:
            break; /* literals and vars: nothing to fold */
    }
}

/* ---- structural simplification & dead-code elimination --------------- */

static void optimize_block(Block *b, bool *changed);

/* Convert an if-statement that we decided to collapse into a plain block
 * wrapping `keep`, freeing the condition and the discarded branch. */
static void collapse_if_to_block(Stmt *s, Block keep, Block *discard) {
    ast_free_expr(s->as.if_stmt.cond);
    block_free(discard);
    s->kind = STMT_BLOCK;
    s->as.block = keep;
}

static void optimize_stmt(Stmt *s, bool *changed) {
    switch (s->kind) {
        case STMT_LET:
            fold_expr(&s->as.let.init, changed);
            break;
        case STMT_EXPR:
            fold_expr(&s->as.expr, changed);
            break;
        case STMT_RETURN:
            if (s->as.ret.value) fold_expr(&s->as.ret.value, changed);
            break;
        case STMT_IF:
            fold_expr(&s->as.if_stmt.cond, changed);
            optimize_block(&s->as.if_stmt.then_blk, changed);
            if (s->as.if_stmt.has_else)
                optimize_block(&s->as.if_stmt.else_blk, changed);

            if (is_bool_lit(s->as.if_stmt.cond)) {
                bool cond = s->as.if_stmt.cond->as.bool_val;
                if (cond) {
                    Block discard;
                    block_init(&discard);
                    if (s->as.if_stmt.has_else) discard = s->as.if_stmt.else_blk;
                    collapse_if_to_block(s, s->as.if_stmt.then_blk, &discard);
                } else {
                    Block keep;
                    block_init(&keep);
                    if (s->as.if_stmt.has_else) keep = s->as.if_stmt.else_blk;
                    /* discard the then branch */
                    collapse_if_to_block(s, keep, &s->as.if_stmt.then_blk);
                }
                *changed = true;
            }
            break;
        case STMT_WHILE:
            fold_expr(&s->as.while_stmt.cond, changed);
            optimize_block(&s->as.while_stmt.body, changed);
            break;
        case STMT_BLOCK:
            optimize_block(&s->as.block, changed);
            break;
    }
}

/* Returns true if executing this statement always ends the function. */
static bool stmt_terminates(const Stmt *s) {
    return s->kind == STMT_RETURN;
}

static void optimize_block(Block *b, bool *changed) {
    for (int i = 0; i < b->count; i++) optimize_stmt(b->stmts[i], changed);

    /* Build a filtered statement list, applying:
     *   - drop while(false){...} bodies entirely
     *   - drop everything after a statement that always returns
     */
    int out = 0;
    bool terminated = false;
    for (int i = 0; i < b->count; i++) {
        Stmt *s = b->stmts[i];

        if (terminated) {            /* unreachable code after return */
            ast_free_stmt(s);
            *changed = true;
            continue;
        }
        if (s->kind == STMT_WHILE && is_bool_lit(s->as.while_stmt.cond) &&
            !s->as.while_stmt.cond->as.bool_val) {
            ast_free_stmt(s);        /* while(false) never runs */
            *changed = true;
            continue;
        }
        b->stmts[out++] = s;
        if (stmt_terminates(s)) terminated = true;
    }
    b->count = out;
}

/* ---- unused (pure) let removal --------------------------------------- */

static int count_uses_block(const Block *b, const char *name);

static int count_uses_expr(const Expr *e, const char *name) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_VAR:
            return strcmp(e->as.var.name, name) == 0 ? 1 : 0;
        case EXPR_ASSIGN:
            /* target counts as a use so we never delete a written binding */
            return (strcmp(e->as.assign.name, name) == 0 ? 1 : 0) +
                   count_uses_expr(e->as.assign.value, name);
        case EXPR_UNARY:
            return count_uses_expr(e->as.unary.operand, name);
        case EXPR_BINARY:
            return count_uses_expr(e->as.binary.left, name) +
                   count_uses_expr(e->as.binary.right, name);
        case EXPR_CALL: {
            int n = 0;
            for (int i = 0; i < e->as.call.arg_count; i++)
                n += count_uses_expr(e->as.call.args[i], name);
            return n;
        }
        default:
            return 0;
    }
}

static int count_uses_stmt(const Stmt *s, const char *name) {
    switch (s->kind) {
        case STMT_LET:    return count_uses_expr(s->as.let.init, name);
        case STMT_EXPR:   return count_uses_expr(s->as.expr, name);
        case STMT_RETURN: return count_uses_expr(s->as.ret.value, name);
        case STMT_IF:
            return count_uses_expr(s->as.if_stmt.cond, name) +
                   count_uses_block(&s->as.if_stmt.then_blk, name) +
                   (s->as.if_stmt.has_else
                        ? count_uses_block(&s->as.if_stmt.else_blk, name) : 0);
        case STMT_WHILE:
            return count_uses_expr(s->as.while_stmt.cond, name) +
                   count_uses_block(&s->as.while_stmt.body, name);
        case STMT_BLOCK:
            return count_uses_block(&s->as.block, name);
    }
    return 0;
}

static int count_uses_block(const Block *b, const char *name) {
    int n = 0;
    for (int i = 0; i < b->count; i++) n += count_uses_stmt(b->stmts[i], name);
    return n;
}

/* Remove `let`s in `b` (recursively) that are pure and never read/assigned
 * anywhere in the whole function body `root`. */
static void remove_unused_in_block(Block *b, const Block *root, bool *changed) {
    int out = 0;
    for (int i = 0; i < b->count; i++) {
        Stmt *s = b->stmts[i];
        switch (s->kind) {
            case STMT_IF:
                remove_unused_in_block(&s->as.if_stmt.then_blk, root, changed);
                if (s->as.if_stmt.has_else)
                    remove_unused_in_block(&s->as.if_stmt.else_blk, root, changed);
                break;
            case STMT_WHILE:
                remove_unused_in_block(&s->as.while_stmt.body, root, changed);
                break;
            case STMT_BLOCK:
                remove_unused_in_block(&s->as.block, root, changed);
                break;
            default:
                break;
        }
        if (s->kind == STMT_LET &&
            expr_is_pure(s->as.let.init) &&
            count_uses_block(root, s->as.let.name) == 0) {
            ast_free_stmt(s);
            *changed = true;
            continue;
        }
        b->stmts[out++] = s;
    }
    b->count = out;
}

/* ---- driver ---------------------------------------------------------- */

void optimize(Program *program) {
    for (int f = 0; f < program->count; f++) {
        Function *fn = program->functions[f];
        bool changed = true;
        while (changed) {
            changed = false;
            optimize_block(&fn->body, &changed);
            remove_unused_in_block(&fn->body, &fn->body, &changed);
        }
    }
}
