#ifndef MIRA_VM_H
#define MIRA_VM_H

#include <stdbool.h>

#include "compiler.h"

/*
 * Stack-based virtual machine.
 *
 * One shared operand stack holds temporaries across all calls.  Each active
 * call gets a CallFrame with its own instruction pointer and a separately
 * allocated array of local slots (parameters first).  Arguments are passed by
 * pushing them on the operand stack; OP_CALL moves them into the callee's
 * locals.  Runtime traps (division by zero, stack overflow) are reported
 * cleanly with a source position and abort execution without crashing.
 */

typedef enum {
    VM_OK,
    VM_RUNTIME_ERROR
} VMResult;

/* Execute `program` starting at its main function.
 * `trace` enables per-instruction stack tracing. Returns VM_OK on success. */
VMResult vm_run(CompiledProgram *program, const char *source_name, bool trace);

#endif /* MIRA_VM_H */
