#ifndef MIRA_TYPECHECK_H
#define MIRA_TYPECHECK_H

#include <stdbool.h>

#include "ast.h"
#include "error.h"

/*
 * Static semantic analysis.  Walks the program, resolves every name, checks
 * operand/argument/return types, verifies that non-void functions return on all
 * paths, and annotates each Expr node with its resolved `type`.  All problems
 * are reported through Diagnostics; returns true iff the program is well typed.
 */
bool typecheck(Program *program, Diagnostics *diag);

#endif /* MIRA_TYPECHECK_H */
