#ifndef MIRA_TOKEN_H
#define MIRA_TOKEN_H

/*
 * Token definitions for the Mira lexer.
 *
 * A Token carries its classification, the exact source text (lexeme) it was
 * scanned from, and the 1-based line/column where the lexeme begins.  The
 * lexeme is a freshly allocated, NUL-terminated copy owned by the token, so
 * the original source buffer may be freed independently of the token stream.
 */

typedef enum {
    /* Literals */
    TOK_INT,        /* 123                              */
    TOK_STRING,     /* "abc" (already unescaped)        */
    TOK_IDENT,      /* foo                              */

    /* Keywords */
    TOK_LET,
    TOK_FN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_RETURN,
    TOK_TRUE,
    TOK_FALSE,
    TOK_INT_TYPE,   /* int    */
    TOK_BOOL_TYPE,  /* bool   */
    TOK_STRING_TYPE,/* string */
    TOK_VOID_TYPE,  /* void   */

    /* Punctuation */
    TOK_LPAREN,     /* (  */
    TOK_RPAREN,     /* )  */
    TOK_LBRACE,     /* {  */
    TOK_RBRACE,     /* }  */
    TOK_COMMA,      /* ,  */
    TOK_SEMICOLON,  /* ;  */
    TOK_COLON,      /* :  */

    /* Operators */
    TOK_PLUS,       /* +  */
    TOK_MINUS,      /* -  */
    TOK_STAR,       /* *  */
    TOK_SLASH,      /* /  */
    TOK_PERCENT,    /* %  */
    TOK_BANG,       /* !  */
    TOK_ASSIGN,     /* =  */
    TOK_EQ,         /* == */
    TOK_NE,         /* != */
    TOK_LT,         /* <  */
    TOK_LE,         /* <= */
    TOK_GT,         /* >  */
    TOK_GE,         /* >= */
    TOK_AND,        /* && */
    TOK_OR,         /* || */

    TOK_EOF,        /* end of input    */
    TOK_ERROR       /* lexical error   */
} TokenType;

typedef struct {
    TokenType type;
    char     *lexeme;   /* owned, NUL-terminated copy of the source text */
    int       line;     /* 1-based */
    int       column;   /* 1-based, column of the first character        */

    /* Pre-decoded literal payloads (valid only for the matching type). */
    long long int_value;    /* TOK_INT                         */
    char     *string_value; /* TOK_STRING: owned, unescaped    */
    int       string_len;   /* TOK_STRING byte length          */
} Token;

const char *token_type_name(TokenType type);

#endif /* MIRA_TOKEN_H */
