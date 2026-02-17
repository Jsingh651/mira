#ifndef MIRA_AST_H
#define MIRA_AST_H

#include <stdbool.h>

#include "types.h"

/*
 * Abstract syntax tree.
 *
 * The tree is the central artefact passed between the parser, the type checker
 * (which fills in the `type` annotations and the `let` slot bookkeeping), the
 * optimizer (which rewrites it in place), and the bytecode compiler.
 *
 * Ownership is strictly hierarchical: a node owns its children, a function owns
 * its body, and the Program owns its functions.  ast_free_program walks the
 * whole tree exactly once.
 */

/* Binary operators, decoupled from token spellings. */
typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_EQ, BIN_NE, BIN_LT, BIN_LE, BIN_GT, BIN_GE,
    BIN_AND, BIN_OR
} BinaryOp;

typedef enum { UN_NEG, UN_NOT } UnaryOp;

typedef enum {
    EXPR_INT,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_VAR,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_ASSIGN,
    EXPR_CALL
} ExprKind;

typedef struct Expr Expr;

struct Expr {
    ExprKind kind;
    Type     type;   /* filled by the type checker */
    int      line;
    int      column;

    union {
        long long int_val;            /* EXPR_INT    */
        bool      bool_val;           /* EXPR_BOOL   */
        struct { char *value; int len; } str; /* EXPR_STRING (owned, unescaped) */
        struct { char *name; int slot; } var; /* EXPR_VAR; slot set by compiler */
        struct { UnaryOp op; Expr *operand; } unary;
        struct { BinaryOp op; Expr *left, *right; } binary;
        struct { char *name; int slot; Expr *value; } assign; /* slot set by compiler */
        struct { char *name; Expr **args; int arg_count; bool is_print; } call;
    } as;
};

typedef enum {
    STMT_LET,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_RETURN,
    STMT_BLOCK
} StmtKind;

typedef struct Stmt Stmt;

/* A lexical block: an owned, growable list of statements. */
typedef struct {
    Stmt **stmts;
    int    count;
    int    capacity;
} Block;

struct Stmt {
    StmtKind kind;
    int      line;
    int      column;

    union {
        struct {
            char *name;
            bool  has_decl_type;   /* true if written as `let x: T = ...` */
            Type  decl_type;       /* valid when has_decl_type            */
            Type  type;            /* resolved type (checker)             */
            Expr *init;            /* required initializer                */
            int   slot;            /* local slot (compiler)               */
            bool  used;            /* set by optimizer DCE analysis       */
        } let;

        Expr *expr;                /* STMT_EXPR */

        struct {
            Expr  *cond;
            Block  then_blk;
            bool   has_else;
            Block  else_blk;
        } if_stmt;

        struct {
            Expr  *cond;
            Block  body;
        } while_stmt;

        struct {
            Expr *value;           /* NULL for `return;` (void) */
        } ret;

        Block block;               /* STMT_BLOCK */
    } as;
};

typedef struct {
    char *name;
    Type  type;
} Param;

typedef struct {
    char  *name;
    Param *params;
    int    param_count;
    Type   return_type;
    Block  body;
    int    line;
    int    column;
    int    num_locals;             /* highest slot count, set by compiler */
} Function;

typedef struct {
    Function **functions;
    int        count;
    int        capacity;
} Program;

/* ---- block helpers --------------------------------------------------- */
void  block_init(Block *b);
void  block_push(Block *b, Stmt *s);
void  block_free(Block *b);

/* ---- expression constructors (each takes ownership of children) ------ */
Expr *expr_int(long long v, int line, int col);
Expr *expr_bool(bool v, int line, int col);
Expr *expr_string(char *value, int len, int line, int col); /* takes value */
Expr *expr_var(char *name, int line, int col);              /* takes name  */
Expr *expr_unary(UnaryOp op, Expr *operand, int line, int col);
Expr *expr_binary(BinaryOp op, Expr *l, Expr *r, int line, int col);
Expr *expr_assign(char *name, Expr *value, int line, int col);
Expr *expr_call(char *name, Expr **args, int argc, int line, int col);

/* ---- statement constructors ------------------------------------------ */
Stmt *stmt_let(char *name, bool has_type, Type t, Expr *init, int line, int col);
Stmt *stmt_expr(Expr *e, int line, int col);
Stmt *stmt_if(Expr *cond, Block then_blk, bool has_else, Block else_blk, int line, int col);
Stmt *stmt_while(Expr *cond, Block body, int line, int col);
Stmt *stmt_return(Expr *value, int line, int col);
Stmt *stmt_block(Block blk, int line, int col);

/* ---- program / function ---------------------------------------------- */
Program  *program_new(void);
void      program_push(Program *p, Function *fn);
Function *function_new(char *name, Param *params, int pc, Type ret,
                       Block body, int line, int col);

/* ---- teardown -------------------------------------------------------- */
void ast_free_expr(Expr *e);
void ast_free_stmt(Stmt *s);
void ast_free_function(Function *fn);
void ast_free_program(Program *p);

/* ---- operator spellings (for AST dump / errors) ---------------------- */
const char *binop_symbol(BinaryOp op);
const char *unop_symbol(UnaryOp op);

#endif /* MIRA_AST_H */
