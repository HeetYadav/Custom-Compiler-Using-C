/*
 * ============================================================
 *  miniMIPS Compiler — Stage 1: Lexer / Tokenizer
 *  Parses C source into a stream of classified tokens.
 * ============================================================
 */

#include "lexer.h"

/* ─── Keyword Table ───────────────────────────────────────── */
static Keyword keywords[] = {
    {"int",      TOK_INT},      {"void",     TOK_VOID},
    {"return",   TOK_RETURN},   {"if",       TOK_IF},
    {"else",     TOK_ELSE},     {"while",    TOK_WHILE},
    {"for",      TOK_FOR},      {"break",    TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"char",     TOK_CHAR},
    {"float",    TOK_FLOAT},    {"double",   TOK_DOUBLE},
    {"long",     TOK_LONG},     {"short",    TOK_SHORT},
    {"unsigned", TOK_UNSIGNED}, {"signed",   TOK_SIGNED},
    {"struct",   TOK_STRUCT},   {"sizeof",   TOK_SIZEOF},
    {NULL, 0}
};

/* ─── Helper: token type → display name ──────────────────── */
const char *tok_name(TokenType t) {
    switch (t) {
        /* Keywords */
        case TOK_INT:           return "INT";
        case TOK_VOID:          return "VOID";
        case TOK_RETURN:        return "RETURN";
        case TOK_IF:            return "IF";
        case TOK_ELSE:          return "ELSE";
        case TOK_WHILE:         return "WHILE";
        case TOK_FOR:           return "FOR";
        case TOK_BREAK:         return "BREAK";
        case TOK_CONTINUE:      return "CONTINUE";
        case TOK_CHAR:          return "CHAR";
        case TOK_FLOAT:         return "FLOAT";
        case TOK_DOUBLE:        return "DOUBLE";
        case TOK_LONG:          return "LONG";
        case TOK_SHORT:         return "SHORT";
        case TOK_UNSIGNED:      return "UNSIGNED";
        case TOK_SIGNED:        return "SIGNED";
        case TOK_STRUCT:        return "STRUCT";
        case TOK_SIZEOF:        return "SIZEOF";
        /* Literals */
        case TOK_INT_LITERAL:   return "INT_LIT";
        case TOK_FLOAT_LITERAL: return "FLOAT_LIT";
        case TOK_CHAR_LITERAL:  return "CHAR_LIT";
        case TOK_STRING_LITERAL:return "STR_LIT";
        /* Identifier */
        case TOK_IDENTIFIER:    return "IDENT";
        /* Arithmetic */
        case TOK_PLUS:          return "PLUS";
        case TOK_MINUS:         return "MINUS";
        case TOK_STAR:          return "STAR";
        case TOK_SLASH:         return "SLASH";
        case TOK_PERCENT:       return "MOD";
        case TOK_PLUS_PLUS:     return "INC";
        case TOK_MINUS_MINUS:   return "DEC";
        /* Assignment */
        case TOK_ASSIGN:        return "ASSIGN";
        case TOK_PLUS_EQ:       return "PLUS_EQ";
        case TOK_MINUS_EQ:      return "MINUS_EQ";
        case TOK_STAR_EQ:       return "STAR_EQ";
        case TOK_SLASH_EQ:      return "SLASH_EQ";
        /* Comparison */
        case TOK_EQ:            return "EQ";
        case TOK_NEQ:           return "NEQ";
        case TOK_LT:            return "LT";
        case TOK_GT:            return "GT";
        case TOK_LTE:           return "LTE";
        case TOK_GTE:           return "GTE";
        /* Logical */
        case TOK_AND:           return "AND";
        case TOK_OR:            return "OR";
        case TOK_NOT:           return "NOT";
        /* Bitwise */
        case TOK_AMP:           return "AMP";
        case TOK_PIPE:          return "PIPE";
        case TOK_CARET:         return "XOR";
        case TOK_TILDE:         return "BITNOT";
        case TOK_LSHIFT:        return "LSHIFT";
        case TOK_RSHIFT:        return "RSHIFT";
        /* Pointer */
        case TOK_ARROW:         return "ARROW";
        /* Delimiters */
        case TOK_LPAREN:        return "LPAREN";
        case TOK_RPAREN:        return "RPAREN";
        case TOK_LBRACE:        return "LBRACE";
        case TOK_RBRACE:        return "RBRACE";
        case TOK_LBRACKET:      return "LBRACK";
        case TOK_RBRACKET:      return "RBRACK";
        case TOK_SEMICOLON:     return "SEMI";
        case TOK_COMMA:         return "COMMA";
        case TOK_DOT:           return "DOT";
        case TOK_COLON:         return "COLON";
        /* Special */
        case TOK_EOF:           return "EOF";
        default:                return "UNKNOWN";
    }
}

/* ─── Lexer State ─────────────────────────────────────────── */
void lexer_init(Lexer *lex, const char *src) {
    lex->src  = src;
    lex->pos  = 0;
    lex->line = 1;
}

static char peek(Lexer *lex)    { return lex->src[lex->pos]; }
static char peek2(Lexer *lex)   { return lex->src[lex->pos + 1]; }
static char advance(Lexer *lex) { return lex->src[lex->pos++]; }

static Token make_tok(TokenType t, const char *val, int line) {
    Token tok;
    tok.type = t;
    tok.line = line;
    strncpy(tok.value, val, 255);
    tok.value[255] = '\0';
    return tok;
}

/* ─── Core: get next token ────────────────────────────────── */
Token next_token(Lexer *lex) {
    /* skip whitespace & comments */
    while (peek(lex)) {
        if (peek(lex) == '\n') { lex->line++; lex->pos++; }
        else if (isspace((unsigned char)peek(lex))) { lex->pos++; }
        /* single-line comment */
        else if (peek(lex) == '/' && peek2(lex) == '/') {
            while (peek(lex) && peek(lex) != '\n') lex->pos++;
        }
        /* multi-line comment */
        else if (peek(lex) == '/' && peek2(lex) == '*') {
            lex->pos += 2;
            while (peek(lex) && !(peek(lex) == '*' && peek2(lex) == '/')) {
                if (peek(lex) == '\n') lex->line++;
                lex->pos++;
            }
            if (peek(lex)) lex->pos += 2;
        }
        else break;
    }

    int line = lex->line;
    char c = peek(lex);

    if (!c) return make_tok(TOK_EOF, "EOF", line);

    /* ── Identifiers & Keywords ── */
    if (isalpha((unsigned char)c) || c == '_') {
        char buf[256]; int i = 0;
        while (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')
            buf[i++] = advance(lex);
        buf[i] = '\0';

        for (int k = 0; keywords[k].word; k++)
            if (strcmp(buf, keywords[k].word) == 0)
                return make_tok(keywords[k].type, buf, line);

        return make_tok(TOK_IDENTIFIER, buf, line);
    }

    /* ── Numeric Literals ── */
    if (isdigit((unsigned char)c)) {
        char buf[256]; int i = 0;
        TokenType t = TOK_INT_LITERAL;
        while (isdigit((unsigned char)peek(lex))) buf[i++] = advance(lex);
        if (peek(lex) == '.') {
            t = TOK_FLOAT_LITERAL;
            buf[i++] = advance(lex);
            while (isdigit((unsigned char)peek(lex))) buf[i++] = advance(lex);
        }
        buf[i] = '\0';
        return make_tok(t, buf, line);
    }

    /* ── String Literal ── */
    if (c == '"') {
        char buf[256]; int i = 0;
        advance(lex); /* skip opening " */
        while (peek(lex) && peek(lex) != '"') {
            if (peek(lex) == '\\') buf[i++] = advance(lex);
            buf[i++] = advance(lex);
        }
        if (peek(lex)) advance(lex); /* skip closing " */
        buf[i] = '\0';
        return make_tok(TOK_STRING_LITERAL, buf, line);
    }

    /* ── Char Literal ── */
    if (c == '\'') {
        char buf[4]; int i = 0;
        advance(lex);
        if (peek(lex) == '\\') buf[i++] = advance(lex);
        buf[i++] = advance(lex);
        if (peek(lex) == '\'') advance(lex);
        buf[i] = '\0';
        return make_tok(TOK_CHAR_LITERAL, buf, line);
    }

    /* ── Operators & Delimiters ── */
    advance(lex); /* consume current char */
    char nxt = peek(lex);

    switch (c) {
        case '+':
            if (nxt == '+') { advance(lex); return make_tok(TOK_PLUS_PLUS,   "++", line); }
            if (nxt == '=') { advance(lex); return make_tok(TOK_PLUS_EQ,     "+=", line); }
            return make_tok(TOK_PLUS, "+", line);
        case '-':
            if (nxt == '-') { advance(lex); return make_tok(TOK_MINUS_MINUS, "--", line); }
            if (nxt == '=') { advance(lex); return make_tok(TOK_MINUS_EQ,    "-=", line); }
            if (nxt == '>') { advance(lex); return make_tok(TOK_ARROW,       "->", line); }
            return make_tok(TOK_MINUS, "-", line);
        case '*':
            if (nxt == '=') { advance(lex); return make_tok(TOK_STAR_EQ,  "*=", line); }
            return make_tok(TOK_STAR, "*", line);
        case '/':
            if (nxt == '=') { advance(lex); return make_tok(TOK_SLASH_EQ, "/=", line); }
            return make_tok(TOK_SLASH, "/", line);
        case '%':  return make_tok(TOK_PERCENT,  "%",  line);
        case '=':
            if (nxt == '=') { advance(lex); return make_tok(TOK_EQ,  "==", line); }
            return make_tok(TOK_ASSIGN, "=", line);
        case '!':
            if (nxt == '=') { advance(lex); return make_tok(TOK_NEQ, "!=", line); }
            return make_tok(TOK_NOT, "!", line);
        case '<':
            if (nxt == '=') { advance(lex); return make_tok(TOK_LTE,    "<=", line); }
            if (nxt == '<') { advance(lex); return make_tok(TOK_LSHIFT, "<<", line); }
            return make_tok(TOK_LT, "<", line);
        case '>':
            if (nxt == '=') { advance(lex); return make_tok(TOK_GTE,    ">=", line); }
            if (nxt == '>') { advance(lex); return make_tok(TOK_RSHIFT, ">>", line); }
            return make_tok(TOK_GT, ">", line);
        case '&':
            if (nxt == '&') { advance(lex); return make_tok(TOK_AND,  "&&", line); }
            return make_tok(TOK_AMP, "&", line);
        case '|':
            if (nxt == '|') { advance(lex); return make_tok(TOK_OR,   "||", line); }
            return make_tok(TOK_PIPE, "|", line);
        case '^':  return make_tok(TOK_CARET,    "^",  line);
        case '~':  return make_tok(TOK_TILDE,    "~",  line);
        case '(':  return make_tok(TOK_LPAREN,   "(",  line);
        case ')':  return make_tok(TOK_RPAREN,   ")",  line);
        case '{':  return make_tok(TOK_LBRACE,   "{",  line);
        case '}':  return make_tok(TOK_RBRACE,   "}",  line);
        case '[':  return make_tok(TOK_LBRACKET, "[",  line);
        case ']':  return make_tok(TOK_RBRACKET, "]",  line);
        case ';':  return make_tok(TOK_SEMICOLON,";",  line);
        case ',':  return make_tok(TOK_COMMA,    ",",  line);
        case '.':  return make_tok(TOK_DOT,      ".",  line);
        case ':':  return make_tok(TOK_COLON,    ":",  line);
    }

    char unk[2] = {c, '\0'};
    return make_tok(TOK_UNKNOWN, unk, line);
}

/* ─── Pretty-print a line of tokens ──────────────────────── */
void print_tokens_inline(const char *source_line) {
    Lexer lex;
    lexer_init(&lex, source_line);

    printf("Input : %s\n", source_line);
    printf("Tokens: ");

    int first = 1;
    while (1) {
        Token t = next_token(&lex);
        if (t.type == TOK_EOF) break;

        if (!first) printf(" → ");
        first = 0;

        const char *name = tok_name(t.type);
        switch (t.type) {
            case TOK_IDENTIFIER:
            case TOK_INT_LITERAL:
            case TOK_FLOAT_LITERAL:
            case TOK_CHAR_LITERAL:
            case TOK_STRING_LITERAL:
                printf("%s[%s]", name, t.value);
                break;
            default:
                printf("%s", name);
        }
    }
    printf("\n\n");
}

/* ─── Table-style token dump (full file) ─────────────────── */
void dump_token_table(const char *src) {
    Lexer lex;
    lexer_init(&lex, src);

    printf("%-6s %-16s %s\n", "LINE", "TYPE", "VALUE");
    printf("%-6s %-16s %s\n", "----", "----", "-----");

    while (1) {
        Token t = next_token(&lex);
        printf("%-6d %-16s %s\n", t.line, tok_name(t.type), t.value);
        if (t.type == TOK_EOF) break;
    }
}

/* ─── File Reader ─────────────────────────────────────────── */
char *read_hehee_file(const char *filepath, long *file_size) {
    const char *ext = strrchr(filepath, '.');
    if (!ext || strcmp(ext, ".hehee") != 0) {
        fprintf(stderr, "Error: File must have a .hehee extension\n");
        exit(1);
    }

    FILE *file = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "Error: File '%s' not found.\n", filepath);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(*file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        fclose(file);
        exit(1);
    }

    size_t bytes_read = fread(buffer, 1, *file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

/* ─── Entry Point ─────────────────────────────────────────── */
/* Compile with -DLEXER_STANDALONE to build lexer as a standalone
 * executable.  When lexer.c is linked into another program (e.g.
 * parser), omit this flag so only one main() is present.        */
#ifdef LEXER_STANDALONE
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>.hehee\n", argv[0]);
        return 1;
    }

    long file_size;
    char *source_code = read_hehee_file(argv[1], &file_size);

    printf("Successfully read '%s'\n", argv[1]);
    printf("File size: %ld characters\n\n", file_size);

    printf("---- Source Code ----\n");
    printf("%s\n\n", source_code);

    print_tokens_inline(source_code);

    free(source_code);
    return 0;
}
#endif /* LEXER_STANDALONE */
