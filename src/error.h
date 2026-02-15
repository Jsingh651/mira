#ifndef MIRA_ERROR_H
#define MIRA_ERROR_H

#include <stdbool.h>

/*
 * Centralised diagnostics.
 *
 * Every pipeline stage reports problems through a single Diagnostics sink so
 * the driver can decide, in one place, whether to continue or abort.  Each
 * message records the source position that triggered it.  Messages are
 * formatted and printed immediately to stderr (so ordering is preserved) and a
 * running count is kept so callers can ask "did anything go wrong?".
 */

typedef struct {
    const char *source_name; /* file name shown in messages */
    int         error_count;
} Diagnostics;

void diag_init(Diagnostics *d, const char *source_name);

/* Report an error at a specific 1-based line/column. */
void diag_error(Diagnostics *d, int line, int column, const char *fmt, ...);

/* Report an error with no meaningful position (e.g. "no main function"). */
void diag_error_nopos(Diagnostics *d, const char *fmt, ...);

static inline bool diag_had_error(const Diagnostics *d) {
    return d->error_count > 0;
}

#endif /* MIRA_ERROR_H */
