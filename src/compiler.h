#ifndef MIRA_COMPILER_H
#define MIRA_COMPILER_H

#include "ast.h"
#include "chunk.h"

/*
 * Bytecode compiler: lowers the optimized, type-checked AST into one Chunk per
 * function.  Each function gets its own flat local-slot layout (parameters
 * occupy the first slots) and its own constant pool.  Forward jumps are
 * back-patched once their targets are known.
 */

typedef struct {
    char *name;        /* owned copy of the function name        */
    int   arity;       /* parameter count                        */
    int   num_locals;  /* slots to allocate per activation       */
    Chunk chunk;
} ObjFunction;

typedef struct {
    ObjFunction *functions;
    int          count;
    int          main_index; /* entry point */
} CompiledProgram;

/* Compile a validated program. Always succeeds (type checking ran first). */
CompiledProgram *compile(Program *ast);

void compiled_program_free(CompiledProgram *p);

#endif /* MIRA_COMPILER_H */
