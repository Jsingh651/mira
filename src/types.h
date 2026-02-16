#ifndef MIRA_TYPES_H
#define MIRA_TYPES_H

/*
 * The Mira static type universe.  TYPE_ERROR is a sentinel produced by the type
 * checker after it has already reported a problem; it suppresses cascading
 * errors because any operation involving TYPE_ERROR simply yields TYPE_ERROR
 * again without complaint.
 */
typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_VOID,
    TYPE_ERROR
} Type;

const char *type_name(Type t);

#endif /* MIRA_TYPES_H */
