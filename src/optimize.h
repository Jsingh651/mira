#ifndef MIRA_OPTIMIZE_H
#define MIRA_OPTIMIZE_H

#include "ast.h"

/*
 * AST-level optimizer.  Runs constant folding and dead-code elimination,
 * iterating to a fixed point because each enables the other (folding a
 * condition to a literal exposes a dead branch; deleting a branch can expose a
 * newly-constant expression).  Rewrites the program in place.  Must run after a
 * successful type check.
 */
void optimize(Program *program);

#endif /* MIRA_OPTIMIZE_H */
