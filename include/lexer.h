/*
 * ============================================================
 *  miniMIPS Compiler — Stage 1: Lexer / Tokenizer
 *  Header file: types, structures, and function declarations.
 * ============================================================
 */

#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─── Token Types ─────────────────────────────────────────── */
typedef enum {
    /* Keywords */
    TOK_INT, TOK_VOID, TOK_RETURN, TOK_IF, TOK_ELSE,
    TOK_WHILE, TOK_FOR, TOK_BREAK, TOK_CONTINUE,
    TOK_CHAR, TOK_FLOAT, TOK_DOUBLE, TOK_LONG, TOK_SHORT,
    TOK_UNSIGNED, TOK_SIGNED, TOK_STRUCT, TOK_SIZEOF,

    /* Literals */
    TOK_INT_LITERAL,    /* e.g.  42        */
    TOK_FLOAT_LITERAL,  /* e.g.  3.14      */
    TOK_CHAR_LITERAL,   /* e.g.  'a'       */
    TOK_STRING_LITERAL, /* e.g.  "hello"   */

    /* Identifier */
    TOK_IDENTIFIER,

    /* Arithmetic Operators */
    TOK_PLUS,           /* +  */
    TOK_MINUS,          /* -  */
    TOK_STAR,           /* *  (also pointer dereference) */
    TOK_SLASH,          /* /  */
    TOK_PERCENT,        /* %  */
    TOK_PLUS_PLUS,      /* ++ */
    TOK_MINUS_MINUS,    /* -- */

    /* Assignment */
    TOK_ASSIGN,         /* =  */
    TOK_PLUS_EQ,        /* += */
    TOK_MINUS_EQ,       /* -= */
    TOK_STAR_EQ,        /* *= */
    TOK_SLASH_EQ,       /* /= */

    /* Comparison */
    TOK_EQ,             /* == */
    TOK_NEQ,            /* != */
    TOK_LT,             /* <  */
    TOK_GT,             /* >  */
    TOK_LTE,            /* <= */
    TOK_GTE,            /* >= */

    /* Logical */
    TOK_AND,            /* && */
    TOK_OR,             /* || */
    TOK_NOT,            /* !  */

    /* Bitwise */
    TOK_AMP,            /* &  (also address-of) */
    TOK_PIPE,           /* |  */
    TOK_CARET,          /* ^  */
    TOK_TILDE,          /* ~  */
    TOK_LSHIFT,         /* << */
    TOK_RSHIFT,         /* >> */

    /* Pointer / Memory */
    TOK_ARROW,          /* -> */

    /* Delimiters */
    TOK_LPAREN,         /* (  */
    TOK_RPAREN,         /* )  */
    TOK_LBRACE,         /* {  */
    TOK_RBRACE,         /* }  */
    TOK_LBRACKET,       /* [  */
    TOK_RBRACKET,       /* ]  */
    TOK_SEMICOLON,      /* ;  */
    TOK_COMMA,          /* ,  */
    TOK_DOT,            /* .  */
    TOK_COLON,          /* :  */

    /* Special */
    TOK_EOF,
    TOK_UNKNOWN
} TokenType;

/* ─── Token Structure ─────────────────────────────────────── */
typedef struct {
    TokenType type;
    char      value[256];   /* raw text of the token */
    int       line;
} Token;

/* ─── Keyword Table Entry ─────────────────────────────────── */
typedef struct {
    const char *word;
    TokenType   type;
} Keyword;

/* ─── Lexer State ─────────────────────────────────────────── */
typedef struct {
    const char *src;
    int         pos;
    int         line;
} Lexer;

/* ─── Function Declarations ───────────────────────────────── */

/**
 * Initialize a Lexer to tokenize the given null-terminated source string.
 */
void lexer_init(Lexer *lex, const char *src);

/**
 * Return the next Token from the lexer, advancing its position.
 * Returns a TOK_EOF token when the end of input is reached.
 */
Token next_token(Lexer *lex);

/**
 * Return a human-readable name string for the given TokenType.
 * e.g. TOK_PLUS -> "PLUS", TOK_IDENTIFIER -> "IDENT"
 */
const char *tok_name(TokenType t);

/**
 * Tokenize a single source line and print all tokens inline
 * in the format: TYPE[value] → TYPE[value] → ...
 */
void print_tokens_inline(const char *source_line);

/**
 * Tokenize the full source string and print a formatted table
 * with columns: LINE | TYPE | VALUE
 */
void dump_token_table(const char *src);

/**
 * Read and return the contents of a .hehee file at the given filepath.
 * Writes the file's byte count into *file_size.
 * Exits with an error message if the file is missing, unreadable,
 * does not have a .hehee extension, or if memory allocation fails.
 * The caller is responsible for free()-ing the returned buffer.
 */
char *read_hehee_file(const char *filepath, long *file_size);

#endif /* LEXER_H */
