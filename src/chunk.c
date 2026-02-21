#include "chunk.h"

#include <stdlib.h>

void chunk_init(Chunk *c) {
    c->code = NULL;
    c->count = 0;
    c->capacity = 0;
    c->lines = NULL;
    c->constants.values = NULL;
    c->constants.count = 0;
    c->constants.capacity = 0;
}

void chunk_free(Chunk *c) {
    for (int i = 0; i < c->constants.count; i++)
        value_release(c->constants.values[i]);
    free(c->constants.values);
    free(c->code);
    free(c->lines);
    chunk_init(c);
}

void chunk_write(Chunk *c, uint8_t byte, int line) {
    if (c->count == c->capacity) {
        c->capacity = c->capacity < 8 ? 8 : c->capacity * 2;
        c->code  = realloc(c->code,  (size_t)c->capacity * sizeof(uint8_t));
        c->lines = realloc(c->lines, (size_t)c->capacity * sizeof(int));
    }
    c->code[c->count]  = byte;
    c->lines[c->count] = line;
    c->count++;
}

int chunk_add_constant(Chunk *c, Value v) {
    /* Deduplicate identical constants so the pool (and disasm) stays compact. */
    for (int i = 0; i < c->constants.count; i++) {
        if (value_equals(c->constants.values[i], v)) {
            value_release(v); /* the pool already holds an equal, owned copy */
            return i;
        }
    }
    ValueArray *a = &c->constants;
    if (a->count == a->capacity) {
        a->capacity = a->capacity < 8 ? 8 : a->capacity * 2;
        a->values = realloc(a->values, (size_t)a->capacity * sizeof(Value));
    }
    a->values[a->count] = v;
    return a->count++;
}
