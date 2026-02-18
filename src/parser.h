#ifndef MIRA_PARSER_H
#define MIRA_PARSER_H

#include "ast.h"
#include "error.h"
#include "lexer.h"

/*
 * Recursive-descent parser with precedence-climbing expression parsing.
 *
 * On success returns a fully built Program (owned by the caller, freed with
 * ast_free_program).  On any syntax error it reports through Diagnostics,
 * attempts to recover at statement/declaration boundaries so multiple errors
 * can surface in one run, and finally returns NULL after freeing partial work.
 */
Program *parse(const TokenList *tokens, Diagnostics *diag);

#endif /* MIRA_PARSER_H */
