#include "compiler.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCALS 256

typedef struct {
    const char *name;  /* borrowed from the AST */
    int         depth;
} Local;

typedef struct {
    Chunk   *chunk;        /* chunk of the function being compiled */
    Local    locals[MAX_LOCALS];
    int      local_count;
    int      scope_depth;
    int      max_locals;   /* high-water mark -> num_locals        */
    Program *ast;          /* for resolving call targets           */
} Compiler;

/* ---- emission helpers ------------------------------------------------ */

static void emit(Compiler *c, uint8_t b, int line) {
    chunk_write(c->chunk, b, line);
}

static void emit_u16(Compiler *c, int value, int line) {
    emit(c, (uint8_t)((value >> 8) & 0xff), line);
    emit(c, (uint8_t)(value & 0xff), line);
}

static void emit_constant(Compiler *c, Value v, int line) {
    int idx = chunk_add_constant(c->chunk, v);
    emit(c, OP_CONST, line);
    emit_u16(c, idx, line);
}

/* Emit a jump with a placeholder operand; return the operand's byte offset. */
static int emit_jump(Compiler *c, uint8_t op, int line) {
    emit(c, op, line);
    emit(c, 0xff, line);
    emit(c, 0xff, line);
    return c->chunk->count - 2;
}

/* Patch a previously emitted forward jump to land at the current position. */
static void patch_jump(Compiler *c, int operand_offset) {
    int jump = c->chunk->count - (operand_offset + 2);
    c->chunk->code[operand_offset]     = (uint8_t)((jump >> 8) & 0xff);
    c->chunk->code[operand_offset + 1] = (uint8_t)(jump & 0xff);
}

/* Emit a backward branch to `loop_start`. */
static void emit_loop(Compiler *c, int loop_start, int line) {
    emit(c, OP_LOOP, line);
    int offset = c->chunk->count + 2 - loop_start;
    emit_u16(c, offset, line);
}

/* ---- scope / local management ---------------------------------------- */

static void begin_scope(Compiler *c) { c->scope_depth++; }

static void end_scope(Compiler *c) {
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth == c->scope_depth) {
        c->local_count--;
    }
    c->scope_depth--;
}

/* Declare a local; returns its slot index. */
static int add_local(Compiler *c, const char *name) {
    int slot = c->local_count;
    c->locals[slot].name = name;
    c->locals[slot].depth = c->scope_depth;
    c->local_count++;
    if (c->local_count > c->max_locals) c->max_locals = c->local_count;
    return slot;
}

/* Resolve a name to its slot, innermost first. */
static int resolve_local(Compiler *c, const char *name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (strcmp(c->locals[i].name, name) == 0) return i;
    }
    return -1; /* type checker guarantees this never happens */
}

static int resolve_function(Compiler *c, const char *name) {
    for (int i = 0; i < c->ast->count; i++) {
        if (strcmp(c->ast->functions[i]->name, name) == 0) return i;
    }
    return -1;
}

/* ---- expression compilation ------------------------------------------ */

static void compile_expr(Compiler *c, Expr *e);

static OpCode binop_opcode(BinaryOp op, Type result_type) {
    switch (op) {
        case BIN_ADD: return result_type == TYPE_STRING ? OP_CONCAT : OP_ADD;
        case BIN_SUB: return OP_SUB;
        case BIN_MUL: return OP_MUL;
        case BIN_DIV: return OP_DIV;
        case BIN_MOD: return OP_MOD;
        case BIN_EQ:  return OP_EQ;
        case BIN_NE:  return OP_NE;
        case BIN_LT:  return OP_LT;
        case BIN_LE:  return OP_LE;
        case BIN_GT:  return OP_GT;
        case BIN_GE:  return OP_GE;
        default:      return OP_ADD; /* AND/OR handled separately */
    }
}

static void compile_binary(Compiler *c, Expr *e) {
    BinaryOp op = e->as.binary.op;

    if (op == BIN_AND) {
        /* a && b : if a is false, leave a (false) and skip b */
        compile_expr(c, e->as.binary.left);
        int end = emit_jump(c, OP_JUMP_IF_FALSE, e->line);
        emit(c, OP_POP, e->line);
        compile_expr(c, e->as.binary.right);
        patch_jump(c, end);
        return;
    }
    if (op == BIN_OR) {
        /* a || b : if a is true, leave a (true) and skip b */
        compile_expr(c, e->as.binary.left);
        int end = emit_jump(c, OP_JUMP_IF_TRUE, e->line);
        emit(c, OP_POP, e->line);
        compile_expr(c, e->as.binary.right);
        patch_jump(c, end);
        return;
    }

    compile_expr(c, e->as.binary.left);
    compile_expr(c, e->as.binary.right);
    /* `+` chooses ADD vs CONCAT based on the (left) operand type. */
    Type t = e->as.binary.left->type;
    emit(c, binop_opcode(op, op == BIN_ADD ? t : e->type), e->line);
}

static void compile_expr(Compiler *c, Expr *e) {
    switch (e->kind) {
        case EXPR_INT:
            emit_constant(c, value_int(e->as.int_val), e->line);
            break;
        case EXPR_BOOL:
            emit_constant(c, value_bool(e->as.bool_val), e->line);
            break;
        case EXPR_STRING:
            emit_constant(c, value_string_copy(e->as.str.value, e->as.str.len),
                          e->line);
            break;
        case EXPR_VAR: {
            int slot = resolve_local(c, e->as.var.name);
            emit(c, OP_LOAD_LOCAL, e->line);
            emit(c, (uint8_t)slot, e->line);
            break;
        }
        case EXPR_UNARY:
            compile_expr(c, e->as.unary.operand);
            emit(c, e->as.unary.op == UN_NEG ? OP_NEG : OP_NOT, e->line);
            break;
        case EXPR_BINARY:
            compile_binary(c, e);
            break;
        case EXPR_ASSIGN: {
            compile_expr(c, e->as.assign.value);
            int slot = resolve_local(c, e->as.assign.name);
            emit(c, OP_STORE_LOCAL, e->line);
            emit(c, (uint8_t)slot, e->line);
            break;
        }
        case EXPR_CALL:
            if (e->as.call.is_print) {
                compile_expr(c, e->as.call.args[0]);
                emit(c, OP_PRINT, e->line);
                emit(c, OP_VOID, e->line); /* the call evaluates to void */
            } else {
                for (int i = 0; i < e->as.call.arg_count; i++)
                    compile_expr(c, e->as.call.args[i]);
                int fi = resolve_function(c, e->as.call.name);
                emit(c, OP_CALL, e->line);
                emit(c, (uint8_t)fi, e->line);
                emit(c, (uint8_t)e->as.call.arg_count, e->line);
            }
            break;
    }
}

/* ---- statement compilation ------------------------------------------- */

static void compile_block(Compiler *c, Block *b);

static void compile_stmt(Compiler *c, Stmt *s) {
    switch (s->kind) {
        case STMT_LET: {
            compile_expr(c, s->as.let.init);
            int slot = add_local(c, s->as.let.name);
            emit(c, OP_STORE_LOCAL, s->line);
            emit(c, (uint8_t)slot, s->line);
            emit(c, OP_POP, s->line); /* discard the initializer's stack copy */
            break;
        }
        case STMT_EXPR:
            compile_expr(c, s->as.expr);
            emit(c, OP_POP, s->line); /* discard the unused result */
            break;
        case STMT_IF: {
            compile_expr(c, s->as.if_stmt.cond);
            int jf = emit_jump(c, OP_JUMP_IF_FALSE, s->line);
            emit(c, OP_POP, s->line);           /* true path: drop cond */
            begin_scope(c);
            compile_block(c, &s->as.if_stmt.then_blk);
            end_scope(c);
            int jend = emit_jump(c, OP_JUMP, s->line);
            patch_jump(c, jf);
            emit(c, OP_POP, s->line);           /* false path: drop cond */
            if (s->as.if_stmt.has_else) {
                begin_scope(c);
                compile_block(c, &s->as.if_stmt.else_blk);
                end_scope(c);
            }
            patch_jump(c, jend);
            break;
        }
        case STMT_WHILE: {
            int loop_start = c->chunk->count;
            compile_expr(c, s->as.while_stmt.cond);
            int jexit = emit_jump(c, OP_JUMP_IF_FALSE, s->line);
            emit(c, OP_POP, s->line);           /* enter body: drop cond */
            begin_scope(c);
            compile_block(c, &s->as.while_stmt.body);
            end_scope(c);
            emit_loop(c, loop_start, s->line);
            patch_jump(c, jexit);
            emit(c, OP_POP, s->line);           /* exit: drop cond */
            break;
        }
        case STMT_RETURN:
            if (s->as.ret.value) compile_expr(c, s->as.ret.value);
            else                 emit(c, OP_VOID, s->line);
            emit(c, OP_RETURN, s->line);
            break;
        case STMT_BLOCK:
            begin_scope(c);
            compile_block(c, &s->as.block);
            end_scope(c);
            break;
    }
}

static void compile_block(Compiler *c, Block *b) {
    for (int i = 0; i < b->count; i++) compile_stmt(c, b->stmts[i]);
}

/* ---- function / program compilation ---------------------------------- */

static void compile_function(Compiler *c, Function *fn, ObjFunction *out) {
    chunk_init(&out->chunk);
    out->name = malloc(strlen(fn->name) + 1);
    memcpy(out->name, fn->name, strlen(fn->name) + 1);
    out->arity = fn->param_count;

    c->chunk = &out->chunk;
    c->local_count = 0;
    c->scope_depth = 0;
    c->max_locals = 0;

    begin_scope(c);
    for (int i = 0; i < fn->param_count; i++)
        add_local(c, fn->params[i].name);
    compile_block(c, &fn->body);
    end_scope(c);

    /* Safety-net return so control never runs off the end of the chunk. */
    emit(c, OP_VOID, fn->line);
    emit(c, OP_RETURN, fn->line);

    out->num_locals = c->max_locals;
}

CompiledProgram *compile(Program *ast) {
    CompiledProgram *p = calloc(1, sizeof(CompiledProgram));
    p->count = ast->count;
    p->functions = calloc((size_t)ast->count, sizeof(ObjFunction));
    p->main_index = 0;

    Compiler c;
    c.ast = ast;

    for (int i = 0; i < ast->count; i++) {
        compile_function(&c, ast->functions[i], &p->functions[i]);
        if (strcmp(ast->functions[i]->name, "main") == 0) p->main_index = i;
    }
    return p;
}

void compiled_program_free(CompiledProgram *p) {
    if (!p) return;
    for (int i = 0; i < p->count; i++) {
        free(p->functions[i].name);
        chunk_free(&p->functions[i].chunk);
    }
    free(p->functions);
    free(p);
}
