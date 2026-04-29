/*
 * ============================================================
 *  miniMIPS Compiler — Stage 2: Parser
 *
 *  Recursive-descent parser.  Reads a .hehee source file,
 *  drives the Stage-1 lexer token-by-token, builds an AST,
 *  and writes a Graphviz DOT file for visual inspection.
 *
 *  Usage:
 *      ./parser  <source>.hehee  [output.dot]
 *
 *  If the second argument is omitted the DOT output is written
 *  to  <source>.dot  in the current directory.
 *
 *  Render with:
 *      dot -Tpng output.dot -o output.png
 *      dot -Tsvg output.dot -o output.svg
 * ============================================================
 */

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════
 *  §1  Node helpers
 * ══════════════════════════════════════════════════════════════ */

static AstNode *make_node(NodeKind kind, const char *text, int line) {
    AstNode *n   = calloc(1, sizeof(AstNode));
    n->kind      = kind;
    n->line      = line;
    n->num_children = 0;
    strncpy(n->text, text ? text : "", 255);
    n->text[255] = '\0';
    return n;
}

static void add_child(AstNode *parent, AstNode *child) {
    if (!child) return;
    if (parent->num_children >= MAX_CHILDREN) {
        fprintf(stderr, "parser: MAX_CHILDREN exceeded on '%s'\n", parent->text);
        return;
    }
    parent->children[parent->num_children++] = child;
}

/* ══════════════════════════════════════════════════════════════
 *  §2  Forward declarations
 * ══════════════════════════════════════════════════════════════ */

static AstNode *parse_decl(Parser *p);
static AstNode *parse_func_decl_named(Parser *p, const char *type_text,
                                       const char *name, int line);
static AstNode *parse_var_decl_named(Parser *p, const char *type_text,
                                      const char *name, int line);
static AstNode *parse_var_decl_type(Parser *p, const char *type_text, int line);
static AstNode *parse_block(Parser *p);
static AstNode *parse_stmt(Parser *p);
static AstNode *parse_if(Parser *p);
static AstNode *parse_while(Parser *p);
static AstNode *parse_for(Parser *p);
static AstNode *parse_return(Parser *p);
static AstNode *parse_expr(Parser *p);
static AstNode *parse_assign(Parser *p);
static AstNode *parse_or(Parser *p);
static AstNode *parse_and(Parser *p);
static AstNode *parse_eq(Parser *p);
static AstNode *parse_rel(Parser *p);
static AstNode *parse_add(Parser *p);
static AstNode *parse_mul(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);

/* ══════════════════════════════════════════════════════════════
 *  §3  Parser primitives
 * ══════════════════════════════════════════════════════════════ */

void parser_init(Parser *p, const char *src) {
    lexer_init(&p->lexer, src);
    p->errors  = 0;
    p->current = next_token(&p->lexer);
}

static Token cur(Parser *p)  { return p->current; }

static Token eat(Parser *p) {
    Token t    = p->current;
    p->current = next_token(&p->lexer);
    return t;
}

static Token expect(Parser *p, TokenType type, const char *ctx) {
    if (p->current.type == type) return eat(p);
    fprintf(stderr,
        "[PARSE ERROR] line %d: expected '%s' in %s, got '%s' ('%s')\n",
        p->current.line, tok_name(type), ctx,
        tok_name(p->current.type), p->current.value);
    p->errors++;
    Token dummy;
    dummy.type = type;
    dummy.line = p->current.line;
    strncpy(dummy.value, "?", 255);
    return dummy;
}

static int is_type_kw(TokenType t) {
    return t == TOK_INT    || t == TOK_VOID    || t == TOK_CHAR   ||
           t == TOK_FLOAT  || t == TOK_DOUBLE  || t == TOK_LONG   ||
           t == TOK_SHORT  || t == TOK_UNSIGNED || t == TOK_SIGNED ||
           t == TOK_STRUCT;
}

static int is_assign_op(TokenType t) {
    return t == TOK_ASSIGN   || t == TOK_PLUS_EQ  || t == TOK_MINUS_EQ ||
           t == TOK_STAR_EQ  || t == TOK_SLASH_EQ;
}

static void collect_type(Parser *p, char *buf, int bufsz) {
    strncpy(buf, cur(p).value, bufsz - 1);
    buf[bufsz - 1] = '\0';
    eat(p);
    while (cur(p).type == TOK_STAR)
        strncat(buf, "*", bufsz - strlen(buf) - 1), eat(p);
}

/* ══════════════════════════════════════════════════════════════
 *  §4  Top-level
 * ══════════════════════════════════════════════════════════════ */

AstNode *parse_program(Parser *p) {
    AstNode *root = make_node(NODE_PROGRAM, "program", 0);
    while (cur(p).type != TOK_EOF) {
        AstNode *d = parse_decl(p);
        if (d) {
            add_child(root, d);
        } else {
            AstNode *err = make_node(NODE_ERROR, cur(p).value, cur(p).line);
            add_child(root, err);
            eat(p);
        }
    }
    return root;
}

static AstNode *parse_decl(Parser *p) {
    if (!is_type_kw(cur(p).type)) {
        fprintf(stderr,
            "[PARSE ERROR] line %d: expected type keyword, got '%s'\n",
            cur(p).line, cur(p).value);
        p->errors++;
        return NULL;
    }
    int  line = cur(p).line;
    char type_text[256];
    collect_type(p, type_text, sizeof(type_text));

    if (cur(p).type != TOK_IDENTIFIER) {
        fprintf(stderr,
            "[PARSE ERROR] line %d: expected identifier after type '%s'\n",
            line, type_text);
        p->errors++;
        return NULL;
    }
    char name[256];
    strncpy(name, cur(p).value, 255); name[255] = '\0';
    int  name_line = cur(p).line;
    eat(p);

    if (cur(p).type == TOK_LPAREN)
        return parse_func_decl_named(p, type_text, name, name_line);
    else
        return parse_var_decl_named(p, type_text, name, name_line);
}

static AstNode *parse_func_decl_named(Parser *p,
                                       const char *type_text,
                                       const char *name,
                                       int line) {
    char label[512];
    snprintf(label, sizeof(label), "func  %s  :  %s", name, type_text);
    AstNode *node = make_node(NODE_FUNC_DECL, label, line);

    expect(p, TOK_LPAREN, "function parameter list");

    AstNode *params = make_node(NODE_BLOCK, "params", line);
    while (cur(p).type != TOK_RPAREN && cur(p).type != TOK_EOF) {
        if (!is_type_kw(cur(p).type)) {
            fprintf(stderr,
                "[PARSE ERROR] line %d: expected type in parameter list\n",
                cur(p).line);
            p->errors++;
            eat(p);
            continue;
        }
        int pl = cur(p).line;
        char ptype[256];
        collect_type(p, ptype, sizeof(ptype));

        char pname[256] = "?";
        if (cur(p).type == TOK_IDENTIFIER) {
            strncpy(pname, cur(p).value, 255);
            eat(p);
        }
        char plabel[514];   /* ptype(255) + "  " (2) + pname(255) + NUL(1) = 513 */
        snprintf(plabel, sizeof(plabel), "%s  %s", ptype, pname);
        add_child(params, make_node(NODE_PARAM, plabel, pl));

        if (cur(p).type == TOK_COMMA) eat(p);
    }
    expect(p, TOK_RPAREN, "function parameter list");
    add_child(node, params);

    if (cur(p).type == TOK_LBRACE)
        add_child(node, parse_block(p));
    else
        expect(p, TOK_SEMICOLON, "function forward declaration");

    return node;
}

static AstNode *parse_var_decl_named(Parser *p,
                                      const char *type_text,
                                      const char *name,
                                      int line) {
    char label[512];
    snprintf(label, sizeof(label), "var  %s  :  %s", name, type_text);
    AstNode *node = make_node(NODE_VAR_DECL, label, line);
    if (cur(p).type == TOK_ASSIGN) {
        eat(p);
        add_child(node, parse_expr(p));
    }
    expect(p, TOK_SEMICOLON, "variable declaration");
    return node;
}

static AstNode *parse_var_decl_type(Parser *p,
                                     const char *type_text,
                                     int line) {
    if (cur(p).type != TOK_IDENTIFIER) {
        fprintf(stderr,
            "[PARSE ERROR] line %d: expected identifier in variable declaration\n",
            line);
        p->errors++;
        return make_node(NODE_ERROR, "missing-ident", line);
    }
    char name[256];
    strncpy(name, cur(p).value, 255); name[255] = '\0';
    int name_line = cur(p).line;
    eat(p);
    return parse_var_decl_named(p, type_text, name, name_line);
}

/* ══════════════════════════════════════════════════════════════
 *  §5  Statements
 * ══════════════════════════════════════════════════════════════ */

static AstNode *parse_block(Parser *p) {
    int line = cur(p).line;
    expect(p, TOK_LBRACE, "block");
    AstNode *block = make_node(NODE_BLOCK, "block", line);
    while (cur(p).type != TOK_RBRACE && cur(p).type != TOK_EOF) {
        AstNode *s = parse_stmt(p);
        if (s) {
            add_child(block, s);
        } else {
            AstNode *err = make_node(NODE_ERROR, cur(p).value, cur(p).line);
            add_child(block, err);
            eat(p);
        }
    }
    expect(p, TOK_RBRACE, "block");
    return block;
}

static AstNode *parse_stmt(Parser *p) {
    TokenType t = cur(p).type;

    if (t == TOK_LBRACE)  return parse_block(p);
    if (t == TOK_IF)      return parse_if(p);
    if (t == TOK_WHILE)   return parse_while(p);
    if (t == TOK_FOR)     return parse_for(p);
    if (t == TOK_RETURN)  return parse_return(p);

    if (t == TOK_BREAK || t == TOK_CONTINUE) {
        char label[32]; strncpy(label, cur(p).value, 31); label[31] = '\0';
        int line = cur(p).line;
        eat(p);
        expect(p, TOK_SEMICOLON, label);
        return make_node(NODE_EXPR_STMT, label, line);
    }

    if (is_type_kw(t)) {
        int  line = cur(p).line;
        char type_text[256];
        collect_type(p, type_text, sizeof(type_text));
        return parse_var_decl_type(p, type_text, line);
    }

    if (t == TOK_SEMICOLON) {
        int line = cur(p).line; eat(p);
        return make_node(NODE_EXPR_STMT, "(empty)", line);
    }

    int line    = cur(p).line;
    AstNode *ex = parse_expr(p);
    expect(p, TOK_SEMICOLON, "expression statement");
    AstNode *es = make_node(NODE_EXPR_STMT, "expr_stmt", line);
    add_child(es, ex);
    return es;
}

static AstNode *parse_if(Parser *p) {
    int line = cur(p).line; eat(p);
    AstNode *node = make_node(NODE_IF, "if", line);
    expect(p, TOK_LPAREN, "if condition");
    add_child(node, parse_expr(p));
    expect(p, TOK_RPAREN, "if condition");
    add_child(node, parse_stmt(p));
    if (cur(p).type == TOK_ELSE) { eat(p); add_child(node, parse_stmt(p)); }
    return node;
}

static AstNode *parse_while(Parser *p) {
    int line = cur(p).line; eat(p);
    AstNode *node = make_node(NODE_WHILE, "while", line);
    expect(p, TOK_LPAREN, "while condition");
    add_child(node, parse_expr(p));
    expect(p, TOK_RPAREN, "while condition");
    add_child(node, parse_stmt(p));
    return node;
}

static AstNode *parse_for(Parser *p) {
    int line = cur(p).line; eat(p);
    AstNode *node = make_node(NODE_FOR, "for", line);
    expect(p, TOK_LPAREN, "for header");

    /* init */
    if (cur(p).type == TOK_SEMICOLON) {
        add_child(node, make_node(NODE_EXPR_STMT, "(empty)", cur(p).line));
        eat(p);
    } else if (is_type_kw(cur(p).type)) {
        int il = cur(p).line; char type_text[256];
        collect_type(p, type_text, sizeof(type_text));
        add_child(node, parse_var_decl_type(p, type_text, il));
    } else {
        int il = cur(p).line;
        AstNode *e = parse_expr(p);
        expect(p, TOK_SEMICOLON, "for init");
        AstNode *es = make_node(NODE_EXPR_STMT, "expr_stmt", il);
        add_child(es, e); add_child(node, es);
    }

    /* condition */
    if (cur(p).type == TOK_SEMICOLON) {
        add_child(node, make_node(NODE_INT_LIT, "1", cur(p).line));
        eat(p);
    } else {
        add_child(node, parse_expr(p));
        expect(p, TOK_SEMICOLON, "for condition");
    }

    /* post */
    if (cur(p).type == TOK_RPAREN)
        add_child(node, make_node(NODE_EXPR_STMT, "(empty)", cur(p).line));
    else
        add_child(node, parse_expr(p));

    expect(p, TOK_RPAREN, "for header");
    add_child(node, parse_stmt(p));
    return node;
}

static AstNode *parse_return(Parser *p) {
    int line = cur(p).line; eat(p);
    AstNode *node = make_node(NODE_RETURN, "return", line);
    if (cur(p).type != TOK_SEMICOLON) add_child(node, parse_expr(p));
    expect(p, TOK_SEMICOLON, "return statement");
    return node;
}

/* ══════════════════════════════════════════════════════════════
 *  §6  Expressions
 * ══════════════════════════════════════════════════════════════ */

static AstNode *parse_expr(Parser *p) { return parse_assign(p); }

static AstNode *parse_assign(Parser *p) {
    AstNode *lhs = parse_or(p);
    if (is_assign_op(cur(p).type)) {
        char op[8]; strncpy(op, cur(p).value, 7); op[7] = '\0';
        int line = cur(p).line; eat(p);
        AstNode *rhs  = parse_assign(p);
        AstNode *node = make_node(NODE_ASSIGN, op, line);
        add_child(node, lhs); add_child(node, rhs);
        return node;
    }
    return lhs;
}

#define BINOP_LEVEL(fn_name, next_fn, condition)                        \
static AstNode *fn_name(Parser *p) {                                    \
    AstNode *lhs = next_fn(p);                                          \
    while (condition) {                                                  \
        char op[8]; strncpy(op, cur(p).value, 7); op[7] = '\0';        \
        int line = cur(p).line; eat(p);                                 \
        AstNode *rhs  = next_fn(p);                                     \
        AstNode *node = make_node(NODE_BINOP, op, line);                \
        add_child(node, lhs); add_child(node, rhs);                     \
        lhs = node;                                                     \
    }                                                                   \
    return lhs;                                                         \
}

BINOP_LEVEL(parse_or,  parse_and,
    cur(p).type == TOK_OR)
BINOP_LEVEL(parse_and, parse_eq,
    cur(p).type == TOK_AND)
BINOP_LEVEL(parse_eq,  parse_rel,
    cur(p).type == TOK_EQ || cur(p).type == TOK_NEQ)
BINOP_LEVEL(parse_rel, parse_add,
    cur(p).type == TOK_LT  || cur(p).type == TOK_GT  ||
    cur(p).type == TOK_LTE || cur(p).type == TOK_GTE)
BINOP_LEVEL(parse_add, parse_mul,
    cur(p).type == TOK_PLUS || cur(p).type == TOK_MINUS)
BINOP_LEVEL(parse_mul, parse_unary,
    cur(p).type == TOK_STAR || cur(p).type == TOK_SLASH ||
    cur(p).type == TOK_PERCENT)

static AstNode *parse_unary(Parser *p) {
    TokenType t = cur(p).type;
    if (t == TOK_NOT || t == TOK_MINUS || t == TOK_TILDE ||
        t == TOK_PLUS_PLUS || t == TOK_MINUS_MINUS || t == TOK_AMP) {
        char op[8]; strncpy(op, cur(p).value, 7); op[7] = '\0';
        int line = cur(p).line; eat(p);
        AstNode *node = make_node(NODE_UNOP, op, line);
        add_child(node, parse_unary(p));
        return node;
    }
    return parse_postfix(p);
}

static AstNode *parse_postfix(Parser *p) {
    AstNode *base = parse_primary(p);
    while (1) {
        if (cur(p).type == TOK_PLUS_PLUS || cur(p).type == TOK_MINUS_MINUS) {
            char op[4]; strncpy(op, cur(p).value, 3); op[3] = '\0';
            int line = cur(p).line; eat(p);
            AstNode *node = make_node(NODE_POSTFIX, op, line);
            add_child(node, base); base = node; continue;
        }
        if (cur(p).type == TOK_LPAREN) {
            int line = cur(p).line; eat(p);
            AstNode *call = make_node(NODE_CALL, "call", line);
            add_child(call, base);
            while (cur(p).type != TOK_RPAREN && cur(p).type != TOK_EOF) {
                add_child(call, parse_expr(p));
                if (cur(p).type == TOK_COMMA) eat(p);
            }
            expect(p, TOK_RPAREN, "function call");
            base = call; continue;
        }
        break;
    }
    return base;
}

static AstNode *parse_primary(Parser *p) {
    Token t = cur(p);
    switch (t.type) {
        case TOK_INT_LITERAL:   eat(p); return make_node(NODE_INT_LIT,   t.value, t.line);
        case TOK_FLOAT_LITERAL: eat(p); return make_node(NODE_FLOAT_LIT, t.value, t.line);
        case TOK_CHAR_LITERAL: {
            eat(p);
            char buf[260]; snprintf(buf, sizeof(buf), "'%s'", t.value);
            return make_node(NODE_CHAR_LIT, buf, t.line);
        }
        case TOK_STRING_LITERAL: {
            eat(p);
            char buf[260]; snprintf(buf, sizeof(buf), "\"%s\"", t.value);
            return make_node(NODE_STR_LIT, buf, t.line);
        }
        case TOK_IDENTIFIER: eat(p); return make_node(NODE_IDENT, t.value, t.line);
        case TOK_LPAREN: {
            eat(p);
            AstNode *inner = parse_expr(p);
            expect(p, TOK_RPAREN, "parenthesised expression");
            return inner;
        }
        default: {
            fprintf(stderr,
                "[PARSE ERROR] line %d: unexpected token '%s' (%s) in expression\n",
                t.line, t.value, tok_name(t.type));
            p->errors++;
            eat(p);
            return make_node(NODE_ERROR, t.value, t.line);
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  §7  AST cleanup
 * ══════════════════════════════════════════════════════════════ */

void ast_free(AstNode *node) {
    if (!node) return;
    for (int i = 0; i < node->num_children; i++) ast_free(node->children[i]);
    free(node);
}

/* ══════════════════════════════════════════════════════════════
 *  §8  Graphviz DOT output
 * ══════════════════════════════════════════════════════════════ */

static const char *node_color(NodeKind k) {
    switch (k) {
        case NODE_PROGRAM:   return "#DDEEFF";
        case NODE_FUNC_DECL: return "#C8E6C9";
        case NODE_VAR_DECL:  return "#F0F4C3";
        case NODE_PARAM:     return "#DCEDC8";
        case NODE_BLOCK:     return "#E8EAF6";
        case NODE_IF:        return "#FCE4EC";
        case NODE_WHILE:
        case NODE_FOR:       return "#FFF3E0";
        case NODE_RETURN:    return "#F3E5F5";
        case NODE_ASSIGN:    return "#FFCCBC";
        case NODE_BINOP:
        case NODE_UNOP:
        case NODE_POSTFIX:   return "#F5F5F5";
        case NODE_CALL:      return "#B2EBF2";
        case NODE_IDENT:     return "#FFFFFF";
        case NODE_INT_LIT:
        case NODE_FLOAT_LIT:
        case NODE_CHAR_LIT:
        case NODE_STR_LIT:   return "#FFF9C4";
        case NODE_EXPR_STMT: return "#ECEFF1";
        case NODE_ERROR:     return "#FFCDD2";
        default:             return "#FFFFFF";
    }
}

static const char *node_shape(NodeKind k) {
    switch (k) {
        case NODE_PROGRAM:
        case NODE_FUNC_DECL:
        case NODE_BLOCK:     return "box";
        case NODE_IF:
        case NODE_WHILE:
        case NODE_FOR:       return "diamond";
        case NODE_RETURN:    return "trapezium";
        case NODE_CALL:      return "ellipse";
        case NODE_ERROR:     return "octagon";
        default:             return "box";
    }
}

static void dot_escape(const char *src, char *dst, int dstsz) {
    int j = 0;
    for (int i = 0; src[i] && j < dstsz - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') dst[j++] = '\\';
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static int dot_id = 0;

static int emit_dot_node(AstNode *node, FILE *out) {
    if (!node) return -1;
    int my_id = dot_id++;
    char escaped[512];
    dot_escape(node->text, escaped, sizeof(escaped));
    fprintf(out,
        "  n%d [label=\"%s\\nline %d\", shape=%s, "
        "style=filled, fillcolor=\"%s\", fontsize=11];\n",
        my_id, escaped, node->line,
        node_shape(node->kind), node_color(node->kind));
    for (int i = 0; i < node->num_children; i++) {
        int cid = emit_dot_node(node->children[i], out);
        if (cid >= 0) fprintf(out, "  n%d -> n%d;\n", my_id, cid);
    }
    return my_id;
}

void ast_to_dot(AstNode *root, FILE *out) {
    dot_id = 0;
    fprintf(out, "digraph AST {\n");
    fprintf(out, "  graph [rankdir=TB, fontname=\"Helvetica\", bgcolor=\"#FAFAFA\"];\n");
    fprintf(out, "  node  [fontname=\"Helvetica\"];\n");
    fprintf(out, "  edge  [color=\"#555555\"];\n");
    emit_dot_node(root, out);
    fprintf(out, "}\n");
}

/* ══════════════════════════════════════════════════════════════
 *  §9  Entry point
 * ══════════════════════════════════════════════════════════════ */

/* Compile with -DPARSER_STANDALONE to build the parser as its own
 * executable. When linked into later stages, omit this flag.    */
#ifdef PARSER_STANDALONE
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source>.hehee [output.dot]\n", argv[0]);
        return 1;
    }

    long  file_size;
    char *source = read_hehee_file(argv[1], &file_size);

    char dot_path[512];
    if (argc >= 3) {
        strncpy(dot_path, argv[2], 511); dot_path[511] = '\0';
    } else {
        strncpy(dot_path, argv[1], 505); dot_path[505] = '\0';
        char *ext = strrchr(dot_path, '.');
        if (ext) strcpy(ext, ".dot");
        else     strncat(dot_path, ".dot", 511 - strlen(dot_path));
    }

    printf("Parsing '%s' ...\n", argv[1]);
    Parser parser;
    parser_init(&parser, source);
    AstNode *ast = parse_program(&parser);

    if (parser.errors == 0)
        printf("Parse successful — no errors.\n");
    else
        printf("Parse finished with %d error(s).\n", parser.errors);

    FILE *dot_file = fopen(dot_path, "w");
    if (!dot_file) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", dot_path);
        ast_free(ast); free(source); return 1;
    }
    ast_to_dot(ast, dot_file);
    fclose(dot_file);

    printf("AST written  →  %s\n", dot_path);
    printf("Render with  :  dot -Tpng %s -o output.png\n", dot_path);

    ast_free(ast);
    free(source);
    return parser.errors ? 1 : 0;
}
#endif /* PARSER_STANDALONE */
