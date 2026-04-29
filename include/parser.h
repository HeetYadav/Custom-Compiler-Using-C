/*
 * ============================================================
 *  miniMIPS Compiler — Stage 2: Parser
 *  Header: AST node kinds, AstNode struct, public API.
 *
 *  Grammar covered (core subset):
 *
 *  program        → decl*
 *  decl           → func_decl | var_decl
 *  func_decl      → type IDENT '(' param_list ')' block
 *  var_decl       → type IDENT ( '=' expr )? ';'
 *  param_list     → ( param ( ',' param )* )?
 *  param          → type IDENT
 *  block          → '{' stmt* '}'
 *  stmt           → var_decl
 *               |   if_stmt
 *               |   while_stmt
 *               |   for_stmt
 *               |   return_stmt
 *               |   expr_stmt
 *               |   block
 *  if_stmt        → 'if' '(' expr ')' stmt ( 'else' stmt )?
 *  while_stmt     → 'while' '(' expr ')' stmt
 *  for_stmt       → 'for' '(' for_init expr? ';' expr? ')' stmt
 *  for_init       → var_decl | expr_stmt | ';'
 *  return_stmt    → 'return' expr? ';'
 *  expr_stmt      → expr ';'
 *  expr           → assign_expr
 *  assign_expr    → IDENT assign_op assign_expr | cond_expr
 *  cond_expr      → or_expr
 *  or_expr        → and_expr ( '||' and_expr )*
 *  and_expr       → eq_expr  ( '&&' eq_expr  )*
 *  eq_expr        → rel_expr ( ('=='|'!=') rel_expr )*
 *  rel_expr       → add_expr ( ('<'|'>'|'<='|'>=') add_expr )*
 *  add_expr       → mul_expr ( ('+'|'-') mul_expr )*
 *  mul_expr       → unary   ( ('*'|'/'|'%') unary  )*
 *  unary          → ('!'|'-'|'~'|'++'|'--') unary | postfix
 *  postfix        → primary ( '++'|'--' | '(' arg_list ')' )*
 *  primary        → INT_LIT | FLOAT_LIT | CHAR_LIT | STR_LIT
 *               |   IDENT | '(' expr ')'
 * ============================================================
 */

#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

/* ─── AST Node Kinds ──────────────────────────────────────── */
typedef enum {
    /* Top-level */
    NODE_PROGRAM,       /* root: list of declarations          */

    /* Declarations */
    NODE_FUNC_DECL,     /* return-type  name  params  body     */
    NODE_VAR_DECL,      /* type  name  (init-expr)?            */
    NODE_PARAM,         /* type  name                          */

    /* Statements */
    NODE_BLOCK,         /* { stmt* }                           */
    NODE_IF,            /* cond  then  (else)?                 */
    NODE_WHILE,         /* cond  body                          */
    NODE_FOR,           /* init  cond  post  body              */
    NODE_RETURN,        /* (expr)?                             */
    NODE_EXPR_STMT,     /* expression used as statement        */

    /* Expressions */
    NODE_ASSIGN,        /* lhs  op  rhs                        */
    NODE_BINOP,         /* lhs  op  rhs                        */
    NODE_UNOP,          /* op  operand                         */
    NODE_POSTFIX,       /* operand  op  (++ / --)              */
    NODE_CALL,          /* callee  arg*                        */
    NODE_IDENT,         /* name                                */
    NODE_INT_LIT,       /* value                               */
    NODE_FLOAT_LIT,     /* value                               */
    NODE_CHAR_LIT,      /* value                               */
    NODE_STR_LIT,       /* value                               */

    /* Sentinel */
    NODE_ERROR          /* inserted on parse error             */
} NodeKind;

/* ─── AST Node ────────────────────────────────────────────── */
#define MAX_CHILDREN 64

typedef struct AstNode {
    NodeKind       kind;
    char           text[256];   /* label: op symbol, name, literal value … */
    int            line;
    int            num_children;
    struct AstNode *children[MAX_CHILDREN];
} AstNode;

/* ─── Parser State ────────────────────────────────────────── */
typedef struct {
    Lexer  lexer;
    Token  current;   /* look-ahead (one token)    */
    int    errors;    /* count of parse errors     */
} Parser;

/* ─── Public API ──────────────────────────────────────────── */

/**
 * Initialise the parser from a null-terminated source string.
 * The lexer is initialised internally; no external Lexer needed.
 */
void parser_init(Parser *p, const char *src);

/**
 * Parse the full source and return the root NODE_PROGRAM node.
 * Returns NULL only on catastrophic failure.
 * On syntax errors a NODE_ERROR child is inserted and parsing
 * continues (error-recovery mode).
 */
AstNode *parse_program(Parser *p);

/**
 * Recursively free an AST rooted at node.
 */
void ast_free(AstNode *node);

/**
 * Emit the AST as a Graphviz DOT file to the given FILE*.
 * Each AST node becomes a labelled box; edges show parent→child.
 */
void ast_to_dot(AstNode *root, FILE *out);

#endif /* PARSER_H */
