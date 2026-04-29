/*
 * ============================================================
 *  miniMIPS Compiler — Stage 3: Semantic Analyser
 *  Header: symbol table, type system, diagnostic types,
 *          and the public analyse() API.
 *
 *  Input  : AstNode* root produced by parse_program()
 *  Output : printed diagnostics + SemanticResult summary
 *
 *  Checks performed
 *  ─────────────────
 *  1. Scoped symbol table
 *     • Variables and functions must be declared before use.
 *     • Redeclaration within the same scope is an error.
 *     • Inner scopes may shadow outer ones (with a warning).
 *
 *  2. Type checking
 *     • Operand types of binary/unary expressions are resolved.
 *     • Assignment LHS / RHS type compatibility is verified.
 *     • Return expression type must match function return type.
 *     • Narrowing conversions (float → int) produce warnings.
 *
 *  3. Control-flow checks
 *     • break / continue only inside a loop body.
 *     • return only inside a function body.
 *
 *  4. Function calls
 *     • Callee must be declared as a function.
 *     • Argument count must match the formal parameter count.
 * ============================================================
 */

#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "parser.h"   /* AstNode, NodeKind – also pulls in lexer.h */

/* ══════════════════════════════════════════════════════════════
 *  Type system
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    TY_UNKNOWN = 0,   /* not yet resolved / error sentinel  */
    TY_VOID,
    TY_INT,           /* int / short / long / signed / unsigned */
    TY_CHAR,
    TY_FLOAT,
    TY_DOUBLE,
    TY_STRING,        /* string literal (char*)             */
    TY_FUNC           /* function symbol                    */
} SemType;

/** Return the printable name of a SemType. */
const char *semtype_name(SemType t);

/** Parse the type string stored in an AST node's text field
 *  (e.g. "var  x  :  int", "func  foo  :  void") and return
 *  the corresponding SemType. */
SemType semtype_from_text(const char *node_text);

/* ══════════════════════════════════════════════════════════════
 *  Symbol table
 * ══════════════════════════════════════════════════════════════ */

#define SYM_HASH_SIZE  64
#define SYM_SCOPE_MAX 128

typedef struct Symbol {
    char          name[256];
    SemType       type;       /* variable type, or TY_FUNC for functions */
    SemType       ret_type;   /* meaningful only when type == TY_FUNC    */
    int           param_count;/* meaningful only when type == TY_FUNC    */
    int           decl_line;
    struct Symbol *next;      /* hash-chain within a scope bucket        */
} Symbol;

typedef struct {
    Symbol *buckets[SYM_HASH_SIZE];
} SymScope;

typedef struct {
    SymScope scopes[SYM_SCOPE_MAX];
    int      depth;           /* index of the innermost live scope       */
} SymTable;

/** Initialise an empty symbol table. */
void symtable_init(SymTable *st);

/** Push a new (empty) scope. */
void symtable_push(SymTable *st);

/** Pop and free the innermost scope. */
void symtable_pop(SymTable *st);

/** Declare a symbol in the current (innermost) scope.
 *  Returns NULL on success, or a pointer to the existing symbol
 *  if the name is already declared in this exact scope.          */
Symbol *symtable_declare(SymTable *st, const char *name,
                          SemType type, SemType ret_type,
                          int param_count, int line);

/** Look up a name through all scopes, innermost first.
 *  Returns NULL if not found.                                    */
Symbol *symtable_lookup(SymTable *st, const char *name);

/** Look up a name in the current scope only.
 *  Used for redeclaration detection.                            */
Symbol *symtable_lookup_current(SymTable *st, const char *name);

/* ══════════════════════════════════════════════════════════════
 *  Diagnostic counters
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    int errors;
    int warnings;
} SemanticResult;

/* ══════════════════════════════════════════════════════════════
 *  Analyser context  (passed through every recursive call)
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    SymTable       symtab;
    SemanticResult result;
    int            loop_depth;  /* >0 → inside a loop              */
    SemType        ret_type;    /* current function's return type   */
    int            in_func;     /* 1 → inside a function body       */
} SemCtx;

/* ══════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════ */

/**
 * Walk the AST rooted at `root` and perform all semantic checks.
 * Diagnostics are printed to stderr.
 * Returns a SemanticResult with error/warning counts.
 */
SemanticResult analyse(AstNode *root);

#endif /* SEMANTIC_H */
