# Mira

A small statically-typed programming language with a full compiler toolchain and stack-based bytecode virtual machine, implemented in C11 with zero third-party dependencies.

## Language

Mira supports four types: `int` (64-bit signed), `bool`, `string` (immutable), and `void` (functions only).

```mira
let x: int = 5;
let y = 10;        // inferred int
let name = "Mira"; // inferred string
let flag = true;
```

**Operators:** arithmetic (`+ - * / %`), comparison (`< <= > >= == !=`), logical (`&& || !` with short-circuit), unary (`-` on int, `!` on bool), assignment (`=`).

**Control flow:** `if` / `else`, `while`, functions with `fn`, recursion, lexical block scoping.

**Built-in:** `print(expr)` prints any value followed by a newline.

### Sample program

```mira
fn fib(n: int): int {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn main(): void {
    let i = 0;
    while (i < 10) {
        print(fib(i));
        i = i + 1;
    }
}
```

Output: `0 1 1 2 3 5 8 13 21 34` (one per line).

## Build & run

```bash
make              # build ./mirac
make test         # run the test suite
make clean        # remove build artifacts
make valgrind     # leak-check all runtime tests (Linux)
```

```bash
./mirac program.mira           # compile and run (entry: main)
./mirac --tokens program.mira  # dump token stream
./mirac --ast program.mira     # pretty-print AST
./mirac --disasm program.mira  # disassemble bytecode
./mirac --no-opt program.mira  # skip optimizer
./mirac --trace program.mira   # trace VM execution
```

## Pipeline

```
source → lexer → parser → type checker → optimizer → compiler → VM
```

1. **Lexer** — Scans source into a token array. Each token carries type, lexeme, line, and column. Handles integer/string literals (with `\n`, `\t`, `\\`, `\"` escapes), keywords, operators, and `//` comments.

2. **Parser** — Recursive descent with precedence climbing for expressions. Builds an AST of functions, blocks, and statements. Reports syntax errors with position and recovers at statement boundaries.

3. **Type checker** — Verifies operand types, bool conditions, assignment compatibility, function signatures, definite returns on all paths, and lexical scoping. Annotates AST nodes with resolved types.

4. **Optimizer** — AST-level constant folding and dead-code elimination, iterated to a fixed point:
   - **Constant folding:** `2 + 3 * 4` → `14`, `true && false` → `false`, string concatenation of literals.
   - **Dead-code elimination:** remove statements after `return`, collapse `if (true/false)`, drop `while (false)`, remove unused pure `let` bindings.

5. **Compiler** — Lowers AST to bytecode. Each function gets its own chunk, constant pool, and local slot layout (parameters occupy slots 0..arity-1). Forward jumps are backpatched once targets are known.

6. **VM** — Stack-based interpreter with operand stack and call frames. Supports the full instruction set, short-circuit via conditional jumps, recursion, and clean runtime traps (division by zero, stack overflow).

## Bytecode format

Each function compiles to a **chunk** of bytes plus a **constant pool** of runtime values.

| Opcode | Operands | Effect |
|--------|----------|--------|
| `OP_CONST` | u16 index | Push `constants[index]` |
| `OP_LOAD_LOCAL` | u8 slot | Push `locals[slot]` |
| `OP_STORE_LOCAL` | u8 slot | `locals[slot] = peek()` (value stays on stack) |
| `OP_POP` | | Discard top |
| `OP_ADD/SUB/MUL/DIV/MOD` | | Int arithmetic |
| `OP_CONCAT` | | String concatenation |
| `OP_NEG` / `OP_NOT` | | Unary int negation / bool not |
| `OP_EQ/NE/LT/LE/GT/GE` | | Comparisons |
| `OP_JUMP` | u16 offset | `ip += offset` |
| `OP_JUMP_IF_FALSE/TRUE` | u16 offset | Conditional forward jump (condition stays on stack) |
| `OP_LOOP` | u16 offset | `ip -= offset` (backward branch) |
| `OP_CALL` | u8 func, u8 argc | Call function by index |
| `OP_RETURN` | | Unwind frame, push return value |
| `OP_PRINT` | | Pop and print value + newline |
| `OP_VOID` | | Push void sentinel |

Multi-byte operands (constant indices, jump offsets) are **big-endian u16**. Local slots and call metadata are **u8**. Every emitted byte records its source line for runtime error reporting.

## Optimizer example

**Before** (`--no-opt --disasm` on `tests/fold.mira`):

```
0000     2  OP_CONST              0  '2'
0003     |  OP_CONST              1  '3'
0006     |  OP_CONST              2  '4'
0009     |  OP_MUL
0010     |  OP_ADD
```

**After** constant folding:

```
0000     2  OP_CONST              0  '14'
```

**Before** DCE (`--no-opt --disasm` on `tests/dce.mira`): dead branches, unreachable `print(4)`, and unused `let unused = 5 + 6` compile to many `OP_CONST`, `OP_JUMP`, and arithmetic instructions.

**After** DCE: only the live `print(2)` and `print(3)` remain — eight instructions instead of thirty-plus.

The optimizer loops until a full pass makes no changes, because folding can expose dead branches and removing dead code can expose new fold opportunities.

## Design decisions

- **Stack VM** — Simple to implement and debug; maps naturally from expression trees; no register allocation needed for a teaching compiler.
- **Flat local slots** — Each function allocates a fixed array indexed by slot number. Parameters are slots 0..N-1; `let` bindings get the next slots. Scopes only affect compile-time name resolution, not runtime layout (slots are not popped on scope exit — acceptable for this language size).
- **Jump backpatching** — Forward jumps emit placeholder u16 operands; once the target offset is known, the compiler patches the two bytes in place.
- **Reference-counted strings** — No GC; every live string Value owns one reference. Copying (push, load) retains; discarding (pop, store-over, teardown) releases.
- **Eager lexing** — The entire token stream is materialized up front so `--tokens` and parser peeking stay simple.

## Requirements

- A C11-capable compiler (`gcc` or `clang`)
- `make` and a POSIX shell to run the test suite

## Project layout

```
mira/
  src/          compiler sources
  tests/        .mira programs, .expected outputs, run_tests.sh
  Makefile
  README.md
```

## Memory

All allocations have explicit owners and matching frees. Teardown releases tokens, AST, bytecode chunks, constant pools, and VM frame locals. On Linux, `make valgrind` runs every runtime test under Valgrind. On macOS, use `leaks --atExit -- ./mirac tests/fib.mira`.
