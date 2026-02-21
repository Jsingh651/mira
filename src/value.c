#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value value_int(long long v) {
    Value val; val.type = VAL_INT; val.as.i = v; return val;
}

Value value_bool(bool v) {
    Value val; val.type = VAL_BOOL; val.as.b = v; return val;
}

Value value_void(void) {
    Value val; val.type = VAL_VOID; val.as.i = 0; return val;
}

Value value_string_take(char *chars, int length) {
    ObjString *s = malloc(sizeof(ObjString));
    s->refcount = 1;
    s->length = length;
    s->chars = chars;
    Value val; val.type = VAL_STRING; val.as.str = s;
    return val;
}

Value value_string_copy(const char *chars, int length) {
    char *buf = malloc((size_t)length + 1);
    memcpy(buf, chars, (size_t)length);
    buf[length] = '\0';
    return value_string_take(buf, length);
}

void value_retain(Value v) {
    if (v.type == VAL_STRING) v.as.str->refcount++;
}

void value_release(Value v) {
    if (v.type == VAL_STRING) {
        if (--v.as.str->refcount == 0) {
            free(v.as.str->chars);
            free(v.as.str);
        }
    }
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_INT:    return a.as.i == b.as.i;
        case VAL_BOOL:   return a.as.b == b.as.b;
        case VAL_VOID:   return true;
        case VAL_STRING:
            return a.as.str->length == b.as.str->length &&
                   memcmp(a.as.str->chars, b.as.str->chars,
                          (size_t)a.as.str->length) == 0;
    }
    return false;
}

void value_print(Value v) {
    switch (v.type) {
        case VAL_INT:    printf("%lld", v.as.i); break;
        case VAL_BOOL:   printf("%s", v.as.b ? "true" : "false"); break;
        case VAL_STRING: fputs(v.as.str->chars, stdout); break;
        case VAL_VOID:   printf("void"); break;
    }
}

void value_print_debug(Value v) {
    switch (v.type) {
        case VAL_INT:    printf("%lld", v.as.i); break;
        case VAL_BOOL:   printf("%s", v.as.b ? "true" : "false"); break;
        case VAL_STRING: printf("\"%s\"", v.as.str->chars); break;
        case VAL_VOID:   printf("void"); break;
    }
}
