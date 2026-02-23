#include "vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK_MAX  4096   /* operand stack capacity            */
#define FRAMES_MAX 512    /* call depth before overflow trap   */

typedef struct {
    ObjFunction *fn;
    uint8_t     *ip;     /* points into fn->chunk.code        */
    Value       *locals; /* heap array, fn->num_locals slots  */
} CallFrame;

typedef struct {
    Value      stack[STACK_MAX];
    Value     *sp;            /* next free slot */
    CallFrame  frames[FRAMES_MAX];
    int        frame_count;
    CompiledProgram *program;
    const char *source_name;
    bool        trace;
    bool        had_error;
} VM;

/* ---- stack helpers --------------------------------------------------- */

static void push(VM *vm, Value v) { *vm->sp++ = v; }
static Value pop(VM *vm)          { return *--vm->sp; }
static Value peek(VM *vm, int d)  { return vm->sp[-1 - d]; }

/* ---- error reporting & teardown -------------------------------------- */

/* Release every live value (operand stack + all frame locals) so a trap mid
 * execution does not leak.  Called once on the way out. */
static void vm_release_all(VM *vm) {
    for (Value *p = vm->stack; p < vm->sp; p++) value_release(*p);
    vm->sp = vm->stack;
    for (int i = 0; i < vm->frame_count; i++) {
        CallFrame *f = &vm->frames[i];
        for (int s = 0; s < f->fn->num_locals; s++) value_release(f->locals[s]);
        free(f->locals);
    }
    vm->frame_count = 0;
}

static void runtime_error(VM *vm, CallFrame *frame, const char *fmt, ...) {
    size_t offset = (size_t)(frame->ip - frame->fn->chunk.code) - 1;
    int line = frame->fn->chunk.lines[offset];
    fprintf(stderr, "%s:%d: runtime error: ", vm->source_name, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    vm->had_error = true;
}

/* ---- call / return --------------------------------------------------- */

/* Set up a new frame for `fn`, moving `argc` args off the operand stack into
 * the new frame's first slots.  Returns false on stack-overflow trap. */
static bool call_function(VM *vm, ObjFunction *fn, int argc, CallFrame *caller) {
    if (vm->frame_count == FRAMES_MAX) {
        runtime_error(vm, caller, "stack overflow (call depth exceeded %d)",
                      FRAMES_MAX);
        return false;
    }
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->fn = fn;
    frame->ip = fn->chunk.code;
    frame->locals = malloc((size_t)fn->num_locals * sizeof(Value));

    /* Args were pushed left-to-right, so the last is on top. Move in reverse. */
    for (int i = argc - 1; i >= 0; i--) frame->locals[i] = pop(vm);
    /* Remaining slots start as void so teardown can release them uniformly. */
    for (int i = argc; i < fn->num_locals; i++) frame->locals[i] = value_void();
    return true;
}

/* ---- the interpreter loop -------------------------------------------- */

VMResult vm_run(CompiledProgram *program, const char *source_name, bool trace) {
    VM vm;
    vm.sp = vm.stack;
    vm.frame_count = 0;
    vm.program = program;
    vm.source_name = source_name;
    vm.trace = trace;
    vm.had_error = false;

    /* Enter main(). It takes no args. */
    if (!call_function(&vm, &program->functions[program->main_index], 0, NULL)) {
        vm_release_all(&vm);
        return VM_RUNTIME_ERROR;
    }

    CallFrame *frame = &vm.frames[vm.frame_count - 1];

#define READ_BYTE()  (*frame->ip++)
#define READ_U16()   (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

    for (;;) {
        if (trace) {
            printf("          ");
            for (Value *p = vm.stack; p < vm.sp; p++) {
                printf("[ ");
                value_print_debug(*p);
                printf(" ]");
            }
            printf("\n");
        }

        uint8_t op = READ_BYTE();
        switch (op) {
            case OP_CONST: {
                uint16_t idx = READ_U16();
                Value v = frame->fn->chunk.constants.values[idx];
                value_retain(v);
                push(&vm, v);
                break;
            }
            case OP_LOAD_LOCAL: {
                uint8_t slot = READ_BYTE();
                Value v = frame->locals[slot];
                value_retain(v);
                push(&vm, v);
                break;
            }
            case OP_STORE_LOCAL: {
                uint8_t slot = READ_BYTE();
                Value v = peek(&vm, 0);     /* leave the value on the stack */
                value_retain(v);            /* the slot takes its own ref    */
                value_release(frame->locals[slot]);
                frame->locals[slot] = v;
                break;
            }
            case OP_POP:
                value_release(pop(&vm));
                break;
            case OP_ADD: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_int(a + b));
                break;
            }
            case OP_SUB: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_int(a - b));
                break;
            }
            case OP_MUL: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_int(a * b));
                break;
            }
            case OP_DIV: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                if (b == 0) { runtime_error(&vm, frame, "division by zero"); goto done; }
                push(&vm, value_int(a / b));
                break;
            }
            case OP_MOD: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                if (b == 0) { runtime_error(&vm, frame, "modulo by zero"); goto done; }
                push(&vm, value_int(a % b));
                break;
            }
            case OP_NEG: {
                long long a = pop(&vm).as.i;
                push(&vm, value_int(-a));
                break;
            }
            case OP_CONCAT: {
                Value b = pop(&vm), a = pop(&vm);
                int len = a.as.str->length + b.as.str->length;
                char *buf = malloc((size_t)len + 1);
                memcpy(buf, a.as.str->chars, (size_t)a.as.str->length);
                memcpy(buf + a.as.str->length, b.as.str->chars,
                       (size_t)b.as.str->length);
                buf[len] = '\0';
                push(&vm, value_string_take(buf, len));
                value_release(a);
                value_release(b);
                break;
            }
            case OP_NOT: {
                bool a = pop(&vm).as.b;
                push(&vm, value_bool(!a));
                break;
            }
            case OP_EQ: {
                Value b = pop(&vm), a = pop(&vm);
                bool r = value_equals(a, b);
                value_release(a); value_release(b);
                push(&vm, value_bool(r));
                break;
            }
            case OP_NE: {
                Value b = pop(&vm), a = pop(&vm);
                bool r = !value_equals(a, b);
                value_release(a); value_release(b);
                push(&vm, value_bool(r));
                break;
            }
            case OP_LT: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_bool(a < b));
                break;
            }
            case OP_LE: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_bool(a <= b));
                break;
            }
            case OP_GT: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_bool(a > b));
                break;
            }
            case OP_GE: {
                long long b = pop(&vm).as.i, a = pop(&vm).as.i;
                push(&vm, value_bool(a >= b));
                break;
            }
            case OP_JUMP: {
                uint16_t off = READ_U16();
                frame->ip += off;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t off = READ_U16();
                if (!peek(&vm, 0).as.b) frame->ip += off;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                uint16_t off = READ_U16();
                if (peek(&vm, 0).as.b) frame->ip += off;
                break;
            }
            case OP_LOOP: {
                uint16_t off = READ_U16();
                frame->ip -= off;
                break;
            }
            case OP_CALL: {
                uint8_t fi = READ_BYTE();
                uint8_t argc = READ_BYTE();
                if (vm.sp - vm.stack > STACK_MAX - 256) {
                    runtime_error(&vm, frame, "operand stack overflow");
                    goto done;
                }
                if (!call_function(&vm, &program->functions[fi], argc, frame))
                    goto done;
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_RETURN: {
                Value rv = pop(&vm);
                /* Release this frame's locals and pop it. */
                for (int s = 0; s < frame->fn->num_locals; s++)
                    value_release(frame->locals[s]);
                free(frame->locals);
                vm.frame_count--;
                if (vm.frame_count == 0) {
                    value_release(rv); /* main returns void; discard */
                    goto done;
                }
                push(&vm, rv);
                frame = &vm.frames[vm.frame_count - 1];
                break;
            }
            case OP_PRINT: {
                Value v = pop(&vm);
                value_print(v);
                putchar('\n');
                value_release(v);
                break;
            }
            case OP_VOID:
                push(&vm, value_void());
                break;
            default:
                runtime_error(&vm, frame, "unknown opcode %d", op);
                goto done;
        }
    }

done:
    vm_release_all(&vm);
    return vm.had_error ? VM_RUNTIME_ERROR : VM_OK;

#undef READ_BYTE
#undef READ_U16
}
