#ifndef MIRA_DEBUG_H
#define MIRA_DEBUG_H

#include "ast.h"
#include "compiler.h"
#include "lexer.h"

/*
 * Human-readable dumps backing the --tokens, --ast, and --disasm flags.
 * Pure inspection helpers: they never mutate the structures they print.
 */

void debug_dump_tokens(const TokenList *tokens);
void debug_print_ast(const Program *program);
void debug_disassemble_program(const CompiledProgram *program);

#endif /* MIRA_DEBUG_H */
