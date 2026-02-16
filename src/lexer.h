#ifndef MIRA_LEXER_H
#define MIRA_LEXER_H

#include <stdbool.h>
#include <stddef.h>

#include "error.h"
#include "token.h"

/*
 * The lexer scans an entire source buffer up front into a heap-allocated,
 * TOK_EOF-terminated array of Tokens.  Doing the whole scan eagerly keeps the
 * parser simple (it can peek arbitrarily far) and lets `--tokens` dump the
 * stream without re-running anything.
 *
 * Lexical errors (unterminated string, stray character, bad escape) are
 * reported through the Diagnostics sink.  Scanning still produces a usable
 * stream so later stages can run if the caller chooses; the EOF token always
 * terminates the array.
 */

typedef struct {
    Token *tokens;  /* owned array, terminated by a TOK_EOF token */
    size_t count;   /* number of tokens including the EOF token   */
} TokenList;

/* Scan `source` into `out`. Returns true if no lexical errors occurred. */
bool lexer_scan(const char *source, Diagnostics *diag, TokenList *out);

/* Free every token and the backing array. */
void token_list_free(TokenList *list);

#endif /* MIRA_LEXER_H */
