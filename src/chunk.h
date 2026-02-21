#ifndef MIRA_CHUNK_H
#define MIRA_CHUNK_H

#include <stddef.h>
#include <stdint.h>

#include "value.h"

/*
 * Bytecode container.
 *
 * Operand encoding:
 *   - constant indices and jump offsets are 16-bit big-endian
 *   - local slots, function indices, and argument counts are 8-bit
 * Each emitted byte records the source line that produced it so the VM can
 * report runtime traps with a position.  A constant pool holds literal Values;
 * string constants own one reference and are released when the chunk is freed.
 */

typedef enum {
    OP_CONST,          /* u16 const index : push constants[idx]        */
    OP_LOAD_LOCAL,     /* u8 slot         : push locals[slot]          */
    OP_STORE_LOCAL,    /* u8 slot         : locals[slot] = peek(); keep */
    OP_POP,            /*                   discard top                */
    OP_ADD,            /* int + int                                    */
    OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_NEG,            /* int negate                                   */
    OP_CONCAT,         /* string + string                             */
    OP_NOT,            /* bool not                                     */
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_JUMP,           /* u16 off : ip += off                          */
    OP_JUMP_IF_FALSE,  /* u16 off : peek; if falsey ip += off          */
    OP_JUMP_IF_TRUE,   /* u16 off : peek; if truthy ip += off          */
    OP_LOOP,           /* u16 off : ip -= off (backward branch)        */
    OP_CALL,           /* u8 func, u8 argc                             */
    OP_RETURN,         /* pop return value, unwind frame               */
    OP_PRINT,          /* pop value, print it + newline                */
    OP_VOID            /* push the void value                          */
} OpCode;

typedef struct {
    Value *values;
    int    count;
    int    capacity;
} ValueArray;

typedef struct {
    uint8_t *code;
    int      count;
    int      capacity;
    int     *lines;       /* parallel to code: source line per byte */
    ValueArray constants;
} Chunk;

void chunk_init(Chunk *c);
void chunk_free(Chunk *c);

/* Append a single byte tagged with its source line. */
void chunk_write(Chunk *c, uint8_t byte, int line);

/* Add a constant (taking ownership of string Values), deduplicating equal
 * entries.  Returns the constant's index. */
int  chunk_add_constant(Chunk *c, Value v);

#endif /* MIRA_CHUNK_H */
