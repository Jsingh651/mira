#ifndef MIRA_VALUE_H
#define MIRA_VALUE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Tagged runtime value.
 *
 * Strings are heap objects shared by reference count.  The discipline is:
 * every Value-of-string that is "live" (on the operand stack, in a local slot,
 * or in a constant pool entry) owns exactly one reference.  Copying a live
 * string Value (push, load) calls value_retain; discarding one (pop, store
 * over, teardown) calls value_release.  When the count hits zero the chars and
 * the object are freed.  This makes the VM leak-free with no global GC.
 */

typedef enum {
    VAL_INT,
    VAL_BOOL,
    VAL_STRING,
    VAL_VOID
} ValueType;

typedef struct {
    int   refcount;
    int   length;   /* byte length, excluding the NUL */
    char *chars;    /* owned, NUL-terminated          */
} ObjString;

typedef struct {
    ValueType type;
    union {
        long long  i;
        bool       b;
        ObjString *str;
    } as;
} Value;

/* Constructors. *_take adopts an existing malloc'd buffer; *_copy duplicates. */
Value value_int(long long v);
Value value_bool(bool v);
Value value_void(void);
Value value_string_take(char *chars, int length);
Value value_string_copy(const char *chars, int length);

/* Reference-count management (no-ops for non-string values). */
void  value_retain(Value v);
void  value_release(Value v);

/* Structural equality for == / != (operands are the same type by typing). */
bool  value_equals(Value a, Value b);

/* Print a value followed by nothing (print() adds the newline itself). */
void  value_print(Value v);
/* Print a compact debug form used by --trace. */
void  value_print_debug(Value v);

#endif /* MIRA_VALUE_H */
