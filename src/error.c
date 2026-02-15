#include "error.h"

#include <stdarg.h>
#include <stdio.h>

void diag_init(Diagnostics *d, const char *source_name) {
    d->source_name = source_name;
    d->error_count = 0;
}

static void vreport(Diagnostics *d, int line, int column,
                    const char *fmt, va_list ap) {
    d->error_count++;
    if (line > 0) {
        fprintf(stderr, "%s:%d:%d: error: ", d->source_name, line, column);
    } else {
        fprintf(stderr, "%s: error: ", d->source_name);
    }
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void diag_error(Diagnostics *d, int line, int column, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vreport(d, line, column, fmt, ap);
    va_end(ap);
}

void diag_error_nopos(Diagnostics *d, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vreport(d, 0, 0, fmt, ap);
    va_end(ap);
}
