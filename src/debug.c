#include "debug.h"

#include <stdio.h>

/* ===================== token dump ====================================== */

void debug_dump_tokens(const TokenList *tokens) {
    for (size_t i = 0; i < tokens->count; i++) {
        const Token *t = &tokens->tokens[i];
        printf("%4d:%-3d  %-12s  '%s'",
               t->line, t->column, token_type_name(t->type), t->lexeme);
        if (t->type == TOK_STRING)
            printf("  (value: \"%s\")", t->string_value);
        printf("\n");
    }
}

/* ===================== AST pretty-printer ============================== */

static void indent(int n) { for (int i = 0; i < n; i++) printf("  "); }

static void print_expr(const Expr *e);

static void print_args(const Expr *e) {
    printf("(");
    for (int i = 0; i < e->as.call.arg_count; i++) {
        if (i) printf(", ");
        print_expr(e->as.call.args[i]);
    }
    printf(")");
}

static void print_expr(const Expr *e) {
    switch (e->kind) {
        case EXPR_INT:    printf("%lld", e->as.int_val); break;
        case EXPR_BOOL:   printf("%s", e->as.bool_val ? "true" : "false"); break;
        case EXPR_STRING: printf("\"%s\"", e->as.str.value); break;
        case EXPR_VAR:    printf("%s", e->as.var.name); break;
        case EXPR_UNARY:
            printf("(%s ", unop_symbol(e->as.unary.op));
            print_expr(e->as.unary.operand);
            printf(")");
            break;
        case EXPR_BINARY:
            printf("(");
            print_expr(e->as.binary.left);
            printf(" %s ", binop_symbol(e->as.binary.op));
            print_expr(e->as.binary.right);
            printf(")");
            break;
        case EXPR_ASSIGN:
            printf("(%s = ", e->as.assign.name);
            print_expr(e->as.assign.value);
            printf(")");
            break;
        case EXPR_CALL:
            printf("%s", e->as.call.name);
            print_args(e);
            break;
    }
}

static void print_block(const Block *b, int depth);

static void print_stmt(const Stmt *s, int depth) {
    indent(depth);
    switch (s->kind) {
        case STMT_LET:
            printf("let %s: %s = ", s->as.let.name, type_name(s->as.let.type));
            print_expr(s->as.let.init);
            printf("\n");
            break;
        case STMT_EXPR:
            print_expr(s->as.expr);
            printf("\n");
            break;
        case STMT_IF:
            printf("if ");
            print_expr(s->as.if_stmt.cond);
            printf("\n");
            print_block(&s->as.if_stmt.then_blk, depth + 1);
            if (s->as.if_stmt.has_else) {
                indent(depth);
                printf("else\n");
                print_block(&s->as.if_stmt.else_blk, depth + 1);
            }
            break;
        case STMT_WHILE:
            printf("while ");
            print_expr(s->as.while_stmt.cond);
            printf("\n");
            print_block(&s->as.while_stmt.body, depth + 1);
            break;
        case STMT_RETURN:
            printf("return");
            if (s->as.ret.value) { printf(" "); print_expr(s->as.ret.value); }
            printf("\n");
            break;
        case STMT_BLOCK:
            printf("block\n");
            print_block(&s->as.block, depth + 1);
            break;
    }
}

static void print_block(const Block *b, int depth) {
    for (int i = 0; i < b->count; i++) print_stmt(b->stmts[i], depth);
}

void debug_print_ast(const Program *program) {
    for (int i = 0; i < program->count; i++) {
        const Function *fn = program->functions[i];
        printf("fn %s(", fn->name);
        for (int j = 0; j < fn->param_count; j++) {
            if (j) printf(", ");
            printf("%s: %s", fn->params[j].name, type_name(fn->params[j].type));
        }
        printf("): %s\n", type_name(fn->return_type));
        print_block(&fn->body, 1);
        printf("\n");
    }
}

/* ===================== disassembler =================================== */

static const char *opcode_name(OpCode op) {
    switch (op) {
        case OP_CONST:         return "OP_CONST";
        case OP_LOAD_LOCAL:    return "OP_LOAD_LOCAL";
        case OP_STORE_LOCAL:   return "OP_STORE_LOCAL";
        case OP_POP:           return "OP_POP";
        case OP_ADD:           return "OP_ADD";
        case OP_SUB:           return "OP_SUB";
        case OP_MUL:           return "OP_MUL";
        case OP_DIV:           return "OP_DIV";
        case OP_MOD:           return "OP_MOD";
        case OP_NEG:           return "OP_NEG";
        case OP_CONCAT:        return "OP_CONCAT";
        case OP_NOT:           return "OP_NOT";
        case OP_EQ:            return "OP_EQ";
        case OP_NE:            return "OP_NE";
        case OP_LT:            return "OP_LT";
        case OP_LE:            return "OP_LE";
        case OP_GT:            return "OP_GT";
        case OP_GE:            return "OP_GE";
        case OP_JUMP:          return "OP_JUMP";
        case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE:  return "OP_JUMP_IF_TRUE";
        case OP_LOOP:          return "OP_LOOP";
        case OP_CALL:          return "OP_CALL";
        case OP_RETURN:        return "OP_RETURN";
        case OP_PRINT:         return "OP_PRINT";
        case OP_VOID:          return "OP_VOID";
    }
    return "OP_???";
}

static int simple(const char *name, int offset) {
    printf("%-18s\n", name);
    return offset + 1;
}

static int byte_operand(const char *name, const Chunk *c, int offset) {
    uint8_t slot = c->code[offset + 1];
    printf("%-18s %4d\n", name, slot);
    return offset + 2;
}

static int u16_operand(const char *name, const Chunk *c, int offset) {
    uint16_t v = (uint16_t)((c->code[offset + 1] << 8) | c->code[offset + 2]);
    printf("%-18s %4d\n", name, v);
    return offset + 3;
}

static int jump_operand(const char *name, const Chunk *c, int offset, int sign) {
    uint16_t v = (uint16_t)((c->code[offset + 1] << 8) | c->code[offset + 2]);
    int target = offset + 3 + sign * (int)v;
    printf("%-18s %4d  -> %d\n", name, v, target);
    return offset + 3;
}

static int const_operand(const Chunk *c, int offset) {
    uint16_t idx = (uint16_t)((c->code[offset + 1] << 8) | c->code[offset + 2]);
    printf("%-18s %4d  '", "OP_CONST", idx);
    value_print_debug(c->constants.values[idx]);
    printf("'\n");
    return offset + 3;
}

static int call_operand(const CompiledProgram *p, const Chunk *c, int offset) {
    uint8_t fi = c->code[offset + 1];
    uint8_t argc = c->code[offset + 2];
    printf("%-18s %4d  argc=%d  (%s)\n", "OP_CALL", fi, argc,
           fi < (uint8_t)p->count ? p->functions[fi].name : "?");
    return offset + 3;
}

static int disasm_instruction(const CompiledProgram *p, const Chunk *c,
                              int offset) {
    printf("%04d  ", offset);
    /* Show the source line, repeating '|' when unchanged from the previous. */
    if (offset > 0 && c->lines[offset] == c->lines[offset - 1])
        printf("   |  ");
    else
        printf("%4d  ", c->lines[offset]);

    OpCode op = c->code[offset];
    switch (op) {
        case OP_CONST:         return const_operand(c, offset);
        case OP_LOAD_LOCAL:    return byte_operand("OP_LOAD_LOCAL", c, offset);
        case OP_STORE_LOCAL:   return byte_operand("OP_STORE_LOCAL", c, offset);
        case OP_JUMP:          return jump_operand("OP_JUMP", c, offset, +1);
        case OP_JUMP_IF_FALSE: return jump_operand("OP_JUMP_IF_FALSE", c, offset, +1);
        case OP_JUMP_IF_TRUE:  return jump_operand("OP_JUMP_IF_TRUE", c, offset, +1);
        case OP_LOOP:          return jump_operand("OP_LOOP", c, offset, -1);
        case OP_CALL:          return call_operand(p, c, offset);
        default:               return simple(opcode_name(op), offset);
    }
    (void)u16_operand; /* reserved for future multi-byte operands */
}

void debug_disassemble_program(const CompiledProgram *program) {
    for (int i = 0; i < program->count; i++) {
        const ObjFunction *fn = &program->functions[i];
        printf("== fn %s (arity=%d, locals=%d) ==\n",
               fn->name, fn->arity, fn->num_locals);
        const Chunk *c = &fn->chunk;
        for (int offset = 0; offset < c->count;)
            offset = disasm_instruction(program, c, offset);
        printf("\n");
    }
}
