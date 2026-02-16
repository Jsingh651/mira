#include "lexer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Mutable scanner state threaded through the scanning routines. */
typedef struct {
    const char  *src;     /* whole source, NUL-terminated */
    size_t       pos;     /* current offset               */
    int          line;    /* 1-based current line         */
    int          col;     /* 1-based current column       */
    Diagnostics *diag;

    Token  *tokens;       /* growable output array */
    size_t  count;
    size_t  capacity;
} Lexer;

/* ---- low level character helpers ------------------------------------- */

static char peek(Lexer *lx)      { return lx->src[lx->pos]; }
static char peek_next(Lexer *lx) { return lx->src[lx->pos] ? lx->src[lx->pos + 1] : '\0'; }
static bool at_end(Lexer *lx)    { return lx->src[lx->pos] == '\0'; }

/* Consume and return one character, tracking line/column position. */
static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

/* ---- token emission -------------------------------------------------- */

static void push_token(Lexer *lx, Token t) {
    if (lx->count == lx->capacity) {
        lx->capacity = lx->capacity < 16 ? 16 : lx->capacity * 2;
        lx->tokens = realloc(lx->tokens, lx->capacity * sizeof(Token));
    }
    lx->tokens[lx->count++] = t;
}

/* Build a zeroed token of `type` whose lexeme is the half-open source range
 * [start, lx->pos), beginning at the recorded line/column. */
static Token make_token(Lexer *lx, TokenType type, size_t start,
                        int line, int col) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.type   = type;
    t.line   = line;
    t.column = col;

    size_t len = lx->pos - start;
    t.lexeme = malloc(len + 1);
    memcpy(t.lexeme, lx->src + start, len);
    t.lexeme[len] = '\0';
    return t;
}

/* ---- scanners for each lexeme class ---------------------------------- */

static const struct { const char *kw; TokenType type; } KEYWORDS[] = {
    {"let",    TOK_LET},        {"fn",     TOK_FN},
    {"if",     TOK_IF},         {"else",   TOK_ELSE},
    {"while",  TOK_WHILE},      {"return", TOK_RETURN},
    {"true",   TOK_TRUE},       {"false",  TOK_FALSE},
    {"int",    TOK_INT_TYPE},   {"bool",   TOK_BOOL_TYPE},
    {"string", TOK_STRING_TYPE},{"void",   TOK_VOID_TYPE},
};

static TokenType keyword_or_ident(const char *text) {
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        if (strcmp(text, KEYWORDS[i].kw) == 0) return KEYWORDS[i].type;
    }
    return TOK_IDENT;
}

static void scan_identifier(Lexer *lx, size_t start, int line, int col) {
    while (is_alnum(peek(lx))) advance(lx);
    Token t = make_token(lx, TOK_IDENT, start, line, col);
    t.type = keyword_or_ident(t.lexeme);
    push_token(lx, t);
}

static void scan_number(Lexer *lx, size_t start, int line, int col) {
    while (is_digit(peek(lx))) advance(lx);
    Token t = make_token(lx, TOK_INT, start, line, col);
    /* The grammar only produces non-negative literals here; unary minus is
     * handled by the parser. strtoll saturates on overflow, which is fine for
     * this teaching language. */
    t.int_value = strtoll(t.lexeme, NULL, 10);
    push_token(lx, t);
}

/* Scan a double-quoted string with escape processing into string_value. */
static void scan_string(Lexer *lx, size_t start, int line, int col) {
    /* Build the decoded value in a growable buffer. */
    size_t cap = 16, len = 0;
    char  *buf = malloc(cap);

    bool terminated = false;
    while (!at_end(lx)) {
        char c = peek(lx);
        if (c == '"') {
            advance(lx); /* consume closing quote */
            terminated = true;
            break;
        }
        if (c == '\n') break; /* strings may not span lines */

        if (c == '\\') {
            advance(lx); /* backslash */
            char e = peek(lx);
            char decoded;
            switch (e) {
                case 'n':  decoded = '\n'; break;
                case 't':  decoded = '\t'; break;
                case '\\': decoded = '\\'; break;
                case '"':  decoded = '"';  break;
                default:
                    diag_error(lx->diag, lx->line, lx->col,
                               "unknown escape sequence '\\%c'",
                               e ? e : '0');
                    decoded = e; /* keep going */
                    break;
            }
            advance(lx); /* the escaped character */
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = decoded;
        } else {
            advance(lx);
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = c;
        }
    }
    buf[len] = '\0';

    if (!terminated) {
        diag_error(lx->diag, line, col, "unterminated string literal");
        Token t = make_token(lx, TOK_ERROR, start, line, col);
        free(buf);
        push_token(lx, t);
        return;
    }

    Token t = make_token(lx, TOK_STRING, start, line, col);
    t.string_value = buf;
    t.string_len   = (int)len;
    push_token(lx, t);
}

/* Emit a fixed punctuation/operator token spanning [start, pos). */
static void emit(Lexer *lx, TokenType type, size_t start, int line, int col) {
    push_token(lx, make_token(lx, type, start, line, col));
}

/* ---- main loop ------------------------------------------------------- */

bool lexer_scan(const char *source, Diagnostics *diag, TokenList *out) {
    Lexer lx;
    lx.src = source;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 1;
    lx.diag = diag;
    lx.tokens = NULL;
    lx.count = 0;
    lx.capacity = 0;

    int start_errors = diag->error_count;

    while (!at_end(&lx)) {
        char c = peek(&lx);
        int line = lx.line, col = lx.col;
        size_t start = lx.pos;

        /* Whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(&lx);
            continue;
        }

        /* Line comment */
        if (c == '/' && peek_next(&lx) == '/') {
            while (!at_end(&lx) && peek(&lx) != '\n') advance(&lx);
            continue;
        }

        if (is_alpha(c)) { advance(&lx); scan_identifier(&lx, start, line, col); continue; }
        if (is_digit(c)) { advance(&lx); scan_number(&lx, start, line, col);     continue; }
        if (c == '"')    { advance(&lx); scan_string(&lx, start, line, col);     continue; }

        advance(&lx); /* consume the first char of the operator/punct */
        switch (c) {
            case '(': emit(&lx, TOK_LPAREN,    start, line, col); break;
            case ')': emit(&lx, TOK_RPAREN,    start, line, col); break;
            case '{': emit(&lx, TOK_LBRACE,    start, line, col); break;
            case '}': emit(&lx, TOK_RBRACE,    start, line, col); break;
            case ',': emit(&lx, TOK_COMMA,     start, line, col); break;
            case ';': emit(&lx, TOK_SEMICOLON, start, line, col); break;
            case ':': emit(&lx, TOK_COLON,     start, line, col); break;
            case '+': emit(&lx, TOK_PLUS,      start, line, col); break;
            case '-': emit(&lx, TOK_MINUS,     start, line, col); break;
            case '*': emit(&lx, TOK_STAR,      start, line, col); break;
            case '/': emit(&lx, TOK_SLASH,     start, line, col); break;
            case '%': emit(&lx, TOK_PERCENT,   start, line, col); break;
            case '!':
                if (peek(&lx) == '=') { advance(&lx); emit(&lx, TOK_NE, start, line, col); }
                else                  { emit(&lx, TOK_BANG, start, line, col); }
                break;
            case '=':
                if (peek(&lx) == '=') { advance(&lx); emit(&lx, TOK_EQ, start, line, col); }
                else                  { emit(&lx, TOK_ASSIGN, start, line, col); }
                break;
            case '<':
                if (peek(&lx) == '=') { advance(&lx); emit(&lx, TOK_LE, start, line, col); }
                else                  { emit(&lx, TOK_LT, start, line, col); }
                break;
            case '>':
                if (peek(&lx) == '=') { advance(&lx); emit(&lx, TOK_GE, start, line, col); }
                else                  { emit(&lx, TOK_GT, start, line, col); }
                break;
            case '&':
                if (peek(&lx) == '&') { advance(&lx); emit(&lx, TOK_AND, start, line, col); }
                else diag_error(diag, line, col, "unexpected character '&' (did you mean '&&'?)");
                break;
            case '|':
                if (peek(&lx) == '|') { advance(&lx); emit(&lx, TOK_OR, start, line, col); }
                else diag_error(diag, line, col, "unexpected character '|' (did you mean '||'?)");
                break;
            default:
                diag_error(diag, line, col, "unexpected character '%c'", c);
                break;
        }
    }

    /* Always terminate with EOF. */
    Token eof;
    memset(&eof, 0, sizeof(eof));
    eof.type = TOK_EOF;
    eof.line = lx.line;
    eof.column = lx.col;
    eof.lexeme = malloc(1);
    eof.lexeme[0] = '\0';
    push_token(&lx, eof);

    out->tokens = lx.tokens;
    out->count = lx.count;
    return diag->error_count == start_errors;
}

void token_list_free(TokenList *list) {
    if (!list->tokens) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->tokens[i].lexeme);
        free(list->tokens[i].string_value);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_INT:         return "INT";
        case TOK_STRING:      return "STRING";
        case TOK_IDENT:       return "IDENT";
        case TOK_LET:         return "LET";
        case TOK_FN:          return "FN";
        case TOK_IF:          return "IF";
        case TOK_ELSE:        return "ELSE";
        case TOK_WHILE:       return "WHILE";
        case TOK_RETURN:      return "RETURN";
        case TOK_TRUE:        return "TRUE";
        case TOK_FALSE:       return "FALSE";
        case TOK_INT_TYPE:    return "INT_TYPE";
        case TOK_BOOL_TYPE:   return "BOOL_TYPE";
        case TOK_STRING_TYPE: return "STRING_TYPE";
        case TOK_VOID_TYPE:   return "VOID_TYPE";
        case TOK_LPAREN:      return "LPAREN";
        case TOK_RPAREN:      return "RPAREN";
        case TOK_LBRACE:      return "LBRACE";
        case TOK_RBRACE:      return "RBRACE";
        case TOK_COMMA:       return "COMMA";
        case TOK_SEMICOLON:   return "SEMICOLON";
        case TOK_COLON:       return "COLON";
        case TOK_PLUS:        return "PLUS";
        case TOK_MINUS:       return "MINUS";
        case TOK_STAR:        return "STAR";
        case TOK_SLASH:       return "SLASH";
        case TOK_PERCENT:     return "PERCENT";
        case TOK_BANG:        return "BANG";
        case TOK_ASSIGN:      return "ASSIGN";
        case TOK_EQ:          return "EQ";
        case TOK_NE:          return "NE";
        case TOK_LT:          return "LT";
        case TOK_LE:          return "LE";
        case TOK_GT:          return "GT";
        case TOK_GE:          return "GE";
        case TOK_AND:         return "AND";
        case TOK_OR:          return "OR";
        case TOK_EOF:         return "EOF";
        case TOK_ERROR:       return "ERROR";
    }
    return "?";
}
