#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compiler.h"
#include "debug.h"
#include "error.h"
#include "lexer.h"
#include "optimize.h"
#include "parser.h"
#include "typecheck.h"
#include "vm.h"

typedef struct {
    bool dump_tokens;
    bool dump_ast;
    bool disasm;
    bool no_opt;
    bool trace;
} Options;

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mirac: could not open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--tokens | --ast | --disasm] [--no-opt] [--trace] <file.mira>\n"
            "\n"
            "  --tokens   dump the token stream and exit\n"
            "  --ast      pretty-print the AST and exit\n"
            "  --disasm   disassemble bytecode and exit\n"
            "  --no-opt   skip the optimizer\n"
            "  --trace    trace VM execution\n"
            "  (default)  compile and run\n",
            prog);
}

static bool parse_args(int argc, char **argv, Options *opts, const char **path) {
    opts->dump_tokens = false;
    opts->dump_ast = false;
    opts->disasm = false;
    opts->no_opt = false;
    opts->trace = false;
    *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0) opts->dump_tokens = true;
        else if (strcmp(argv[i], "--ast") == 0) opts->dump_ast = true;
        else if (strcmp(argv[i], "--disasm") == 0) opts->disasm = true;
        else if (strcmp(argv[i], "--no-opt") == 0) opts->no_opt = true;
        else if (strcmp(argv[i], "--trace") == 0) opts->trace = true;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "mirac: unknown flag '%s'\n", argv[i]);
            return false;
        } else if (*path) {
            fprintf(stderr, "mirac: unexpected argument '%s'\n", argv[i]);
            return false;
        } else {
            *path = argv[i];
        }
    }

    if (!*path) {
        print_usage(argv[0]);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    Options opts;
    const char *path;
    if (!parse_args(argc, argv, &opts, &path)) return 1;

    size_t src_len;
    char *source = read_file(path, &src_len);
    if (!source) return 1;

    Diagnostics diag;
    diag_init(&diag, path);

    TokenList tokens;
    if (!lexer_scan(source, &diag, &tokens)) {
        free(source);
        token_list_free(&tokens);
        return 1;
    }

    if (opts.dump_tokens) {
        debug_dump_tokens(&tokens);
        token_list_free(&tokens);
        free(source);
        return 0;
    }

    Program *program = parse(&tokens, &diag);
    token_list_free(&tokens);
    if (!program) {
        free(source);
        return 1;
    }

    if (opts.dump_ast) {
        debug_print_ast(program);
        ast_free_program(program);
        free(source);
        return 0;
    }

    if (!typecheck(program, &diag)) {
        ast_free_program(program);
        free(source);
        return 1;
    }

    if (!opts.no_opt) optimize(program);

    CompiledProgram *compiled = compile(program);
    ast_free_program(program);

    if (opts.disasm) {
        debug_disassemble_program(compiled);
        compiled_program_free(compiled);
        free(source);
        return 0;
    }

    VMResult result = vm_run(compiled, path, opts.trace);
    compiled_program_free(compiled);
    free(source);
    return result == VM_OK ? 0 : 1;
}
