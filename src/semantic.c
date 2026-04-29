/*
 * ============================================================
 *  miniMIPS Compiler — Stage 3: Semantic Analyser
 *
 *  Walks the AstNode tree built by the parser and performs:
 *    • scoped symbol declaration and use-before-declaration
 *    • redeclaration detection (error) and shadowing (warning)
 *    • type resolution for expressions
 *    • type-compatibility checks on assignments and returns
 *    • narrowing-conversion warnings  (float/double → int/char)
 *    • break / continue outside loop  (error)
 *    • return outside function        (error)
 *    • function call arity checking   (error)
 *    • call to non-function symbol    (error)
 *
 *  Usage:
 *      ./semantic  <source>.hehee
 *
 *  The file is parsed internally (same pipeline as parser.c).
 *  Diagnostics go to stderr; a summary is printed to stdout.
 * ============================================================
 */

#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ══════════════════════════════════════════════════════════════
 *  §1  Type helpers
 * ══════════════════════════════════════════════════════════════ */

const char *semtype_name(SemType t) {
    switch (t) {
        case TY_VOID:    return "void";
        case TY_INT:     return "int";
        case TY_CHAR:    return "char";
        case TY_FLOAT:   return "float";
        case TY_DOUBLE:  return "double";
        case TY_STRING:  return "char*";
        case TY_FUNC:    return "<function>";
        default:         return "<unknown>";
    }
}

SemType semtype_from_text(const char *node_text) {
    const char *colon = strrchr(node_text, ':');
    if (!colon) return TY_UNKNOWN;
    const char *ty = colon + 1;
    while (*ty == ' ' || *ty == '\t') ty++;

    if (strncmp(ty, "int",      3) == 0) return TY_INT;
    if (strncmp(ty, "char",     4) == 0) return TY_CHAR;
    if (strncmp(ty, "float",    5) == 0) return TY_FLOAT;
    if (strncmp(ty, "double",   6) == 0) return TY_DOUBLE;
    if (strncmp(ty, "void",     4) == 0) return TY_VOID;
    if (strncmp(ty, "long",     4) == 0) return TY_INT;
    if (strncmp(ty, "short",    5) == 0) return TY_INT;
    if (strncmp(ty, "unsigned", 8) == 0) return TY_INT;
    if (strncmp(ty, "signed",   6) == 0) return TY_INT;
    return TY_UNKNOWN;
}

static void extract_name(const char *text, char *dst, int cap) {
    const char *p = text;
    while (*p && !isspace((unsigned char)*p)) p++;
    while (*p &&  isspace((unsigned char)*p)) p++;
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < cap - 1)
        dst[i++] = *p++;
    dst[i] = '\0';
}

static int count_params(AstNode *params_block) {
    if (!params_block) return 0;
    int n = 0;
    for (int i = 0; i < params_block->num_children; i++)
        if (params_block->children[i]->kind == NODE_PARAM) n++;
    return n;
}

/* ══════════════════════════════════════════════════════════════
 *  §2  Symbol table
 * ══════════════════════════════════════════════════════════════ */

static unsigned hash_name(const char *s) {
    unsigned h = 5381;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h % SYM_HASH_SIZE;
}

void symtable_init(SymTable *st) { st->depth = -1; }

void symtable_push(SymTable *st) {
    if (++st->depth >= SYM_SCOPE_MAX) {
        fprintf(stderr, "semantic: scope stack overflow\n"); exit(1);
    }
    memset(&st->scopes[st->depth], 0, sizeof(SymScope));
}

void symtable_pop(SymTable *st) {
    if (st->depth < 0) return;
    for (int i = 0; i < SYM_HASH_SIZE; i++) {
        Symbol *s = st->scopes[st->depth].buckets[i];
        while (s) { Symbol *nx = s->next; free(s); s = nx; }
        st->scopes[st->depth].buckets[i] = NULL;
    }
    st->depth--;
}

Symbol *symtable_lookup_current(SymTable *st, const char *name) {
    if (st->depth < 0) return NULL;
    unsigned h = hash_name(name);
    Symbol *s  = st->scopes[st->depth].buckets[h];
    while (s) { if (!strcmp(s->name, name)) return s; s = s->next; }
    return NULL;
}

Symbol *symtable_lookup(SymTable *st, const char *name) {
    for (int d = st->depth; d >= 0; d--) {
        unsigned h = hash_name(name);
        Symbol *s  = st->scopes[d].buckets[h];
        while (s) { if (!strcmp(s->name, name)) return s; s = s->next; }
    }
    return NULL;
}

Symbol *symtable_declare(SymTable *st, const char *name,
                          SemType type, SemType ret_type,
                          int param_count, int line) {
    Symbol *existing = symtable_lookup_current(st, name);
    if (existing) return existing;

    Symbol *sym      = calloc(1, sizeof(Symbol));
    strncpy(sym->name, name, 255);
    sym->type        = type;
    sym->ret_type    = ret_type;
    sym->param_count = param_count;
    sym->decl_line   = line;

    unsigned h = hash_name(name);
    sym->next  = st->scopes[st->depth].buckets[h];
    st->scopes[st->depth].buckets[h] = sym;
    return NULL;
}

/* ══════════════════════════════════════════════════════════════
 *  §3  Diagnostics
 * ══════════════════════════════════════════════════════════════ */

static void sem_error(SemCtx *ctx, int line, const char *msg) {
    fprintf(stderr, "[SEMANTIC ERROR]   line %d: %s\n", line, msg);
    ctx->result.errors++;
}

static void sem_warn(SemCtx *ctx, int line, const char *msg) {
    fprintf(stderr, "[SEMANTIC WARNING] line %d: %s\n", line, msg);
    ctx->result.warnings++;
}

/* ══════════════════════════════════════════════════════════════
 *  §4  Type compatibility
 * ══════════════════════════════════════════════════════════════ */

static int types_compatible(SemType lhs, SemType rhs) {
    if (lhs == rhs) return 1;
    if (lhs == TY_UNKNOWN || rhs == TY_UNKNOWN) return 1;
    int ln = (lhs==TY_INT||lhs==TY_CHAR||lhs==TY_FLOAT||lhs==TY_DOUBLE);
    int rn = (rhs==TY_INT||rhs==TY_CHAR||rhs==TY_FLOAT||rhs==TY_DOUBLE);
    if (ln && rn) return 1;
    return 0;
}

static int is_narrowing(SemType lhs, SemType rhs) {
    return (lhs == TY_INT || lhs == TY_CHAR) &&
           (rhs == TY_FLOAT || rhs == TY_DOUBLE);
}

/* ══════════════════════════════════════════════════════════════
 *  §5  Forward declarations
 * ══════════════════════════════════════════════════════════════ */

static void    analyse_node(SemCtx *ctx, AstNode *node);
static SemType analyse_expr(SemCtx *ctx, AstNode *node);

/* ══════════════════════════════════════════════════════════════
 *  §6  Expression type resolver
 * ══════════════════════════════════════════════════════════════ */

static SemType analyse_expr(SemCtx *ctx, AstNode *node) {
    if (!node) return TY_UNKNOWN;

    switch (node->kind) {
        case NODE_INT_LIT:   return TY_INT;
        case NODE_FLOAT_LIT: return TY_DOUBLE;
        case NODE_CHAR_LIT:  return TY_CHAR;
        case NODE_STR_LIT:   return TY_STRING;

        case NODE_IDENT: {
            Symbol *sym = symtable_lookup(&ctx->symtab, node->text);
            if (!sym) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "use of undeclared identifier '%s'", node->text);
                sem_error(ctx, node->line, msg);
                return TY_UNKNOWN;
            }
            return sym->type == TY_FUNC ? sym->ret_type : sym->type;
        }

        case NODE_ASSIGN: {
            if (node->num_children < 2) return TY_UNKNOWN;
            SemType lhs = analyse_expr(ctx, node->children[0]);
            SemType rhs = analyse_expr(ctx, node->children[1]);
            if (!types_compatible(lhs, rhs)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "type mismatch in assignment: cannot assign '%s' to '%s'",
                    semtype_name(rhs), semtype_name(lhs));
                sem_error(ctx, node->line, msg);
            } else if (is_narrowing(lhs, rhs)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "narrowing conversion: assigning '%s' to '%s'",
                    semtype_name(rhs), semtype_name(lhs));
                sem_warn(ctx, node->line, msg);
            }
            return lhs;
        }

        case NODE_BINOP: {
            if (node->num_children < 2) return TY_UNKNOWN;
            SemType l = analyse_expr(ctx, node->children[0]);
            SemType r = analyse_expr(ctx, node->children[1]);
            const char *op = node->text;
            if (!strcmp(op,"==")||!strcmp(op,"!=")||
                !strcmp(op,"<") ||!strcmp(op,">") ||
                !strcmp(op,"<=")||!strcmp(op,">=")||
                !strcmp(op,"&&")||!strcmp(op,"||"))
                return TY_INT;
            if (l == TY_DOUBLE || r == TY_DOUBLE) return TY_DOUBLE;
            if (l == TY_FLOAT  || r == TY_FLOAT)  return TY_FLOAT;
            return TY_INT;
        }

        case NODE_UNOP: {
            if (node->num_children < 1) return TY_UNKNOWN;
            SemType operand = analyse_expr(ctx, node->children[0]);
            if (!strcmp(node->text, "!")) return TY_INT;
            return operand;
        }

        case NODE_POSTFIX: {
            if (node->num_children < 1) return TY_UNKNOWN;
            return analyse_expr(ctx, node->children[0]);
        }

        case NODE_CALL: {
            if (node->num_children < 1) return TY_UNKNOWN;
            AstNode *callee_node = node->children[0];

            if (callee_node->kind != NODE_IDENT) {
                sem_error(ctx, node->line,
                    "callee of function call is not an identifier");
                for (int i = 1; i < node->num_children; i++)
                    analyse_expr(ctx, node->children[i]);
                return TY_UNKNOWN;
            }

            Symbol *sym = symtable_lookup(&ctx->symtab, callee_node->text);
            if (!sym) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "call to undeclared function '%s'", callee_node->text);
                sem_error(ctx, node->line, msg);
                for (int i = 1; i < node->num_children; i++)
                    analyse_expr(ctx, node->children[i]);
                return TY_UNKNOWN;
            }
            if (sym->type != TY_FUNC) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "'%s' is not a function (declared as '%s' on line %d)",
                    callee_node->text, semtype_name(sym->type),
                    sym->decl_line);
                sem_error(ctx, node->line, msg);
                for (int i = 1; i < node->num_children; i++)
                    analyse_expr(ctx, node->children[i]);
                return TY_UNKNOWN;
            }

            int arg_count = node->num_children - 1;
            if (arg_count != sym->param_count) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "function '%s' expects %d argument(s), got %d",
                    callee_node->text, sym->param_count, arg_count);
                sem_error(ctx, node->line, msg);
            }
            for (int i = 1; i < node->num_children; i++)
                analyse_expr(ctx, node->children[i]);
            return sym->ret_type;
        }

        case NODE_ERROR: return TY_UNKNOWN;
        default:         return TY_UNKNOWN;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  §7  Statement / declaration walker
 * ══════════════════════════════════════════════════════════════ */

static void analyse_node(SemCtx *ctx, AstNode *node) {
    if (!node || node->kind == NODE_ERROR) return;

    switch (node->kind) {

        case NODE_PROGRAM: {
            /* hoist function signatures first */
            for (int i = 0; i < node->num_children; i++) {
                AstNode *child = node->children[i];
                if (child->kind != NODE_FUNC_DECL) continue;
                char name[256];
                extract_name(child->text, name, sizeof(name));
                SemType ret = semtype_from_text(child->text);
                int nparams = (child->num_children > 0)
                              ? count_params(child->children[0]) : 0;
                Symbol *clash = symtable_declare(
                    &ctx->symtab, name, TY_FUNC, ret, nparams, child->line);
                if (clash) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                        "redeclaration of function '%s' "
                        "(first declared at line %d)",
                        name, clash->decl_line);
                    sem_error(ctx, child->line, msg);
                }
            }
            for (int i = 0; i < node->num_children; i++)
                analyse_node(ctx, node->children[i]);
            break;
        }

        case NODE_FUNC_DECL: {
            char name[256];
            extract_name(node->text, name, sizeof(name));
            SemType ret = semtype_from_text(node->text);

            symtable_push(&ctx->symtab);
            SemType saved_ret    = ctx->ret_type;
            int     saved_infunc = ctx->in_func;
            ctx->ret_type = ret;
            ctx->in_func  = 1;

            if (node->num_children >= 1) {
                AstNode *params = node->children[0];
                for (int i = 0; i < params->num_children; i++) {
                    AstNode *param = params->children[i];
                    if (param->kind != NODE_PARAM) continue;
                    char pname[256];
                    extract_name(param->text, pname, sizeof(pname));
                    SemType ptype = semtype_from_text(param->text);
                    Symbol *clash = symtable_declare(
                        &ctx->symtab, pname, ptype, TY_UNKNOWN, 0,
                        param->line);
                    if (clash) {
                        char msg[512];
                        snprintf(msg, sizeof(msg),
                            "duplicate parameter name '%s'", pname);
                        sem_error(ctx, param->line, msg);
                    }
                }
            }
            if (node->num_children >= 2)
                analyse_node(ctx, node->children[1]);

            symtable_pop(&ctx->symtab);
            ctx->ret_type = saved_ret;
            ctx->in_func  = saved_infunc;
            break;
        }

        case NODE_VAR_DECL: {
            char name[256];
            extract_name(node->text, name, sizeof(name));
            SemType vtype = semtype_from_text(node->text);

            /* shadowing check */
            Symbol *outer = symtable_lookup(&ctx->symtab, name);
            if (outer) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "'%s' shadows outer declaration from line %d",
                    name, outer->decl_line);
                sem_warn(ctx, node->line, msg);
            }

            Symbol *clash = symtable_declare(
                &ctx->symtab, name, vtype, TY_UNKNOWN, 0, node->line);
            if (clash) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "redeclaration of '%s' in same scope "
                    "(first declared at line %d)",
                    name, clash->decl_line);
                sem_error(ctx, node->line, msg);
            }

            if (node->num_children > 0) {
                SemType init_type = analyse_expr(ctx, node->children[0]);
                if (!types_compatible(vtype, init_type)) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                        "cannot initialise '%s' (%s) with value of type '%s'",
                        name, semtype_name(vtype), semtype_name(init_type));
                    sem_error(ctx, node->line, msg);
                } else if (is_narrowing(vtype, init_type)) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                        "narrowing conversion initialising '%s' (%s) "
                        "from '%s'",
                        name, semtype_name(vtype), semtype_name(init_type));
                    sem_warn(ctx, node->line, msg);
                }
            }
            break;
        }

        case NODE_BLOCK: {
            symtable_push(&ctx->symtab);
            for (int i = 0; i < node->num_children; i++)
                analyse_node(ctx, node->children[i]);
            symtable_pop(&ctx->symtab);
            break;
        }

        case NODE_IF: {
            if (node->num_children >= 1)
                analyse_expr(ctx, node->children[0]);
            if (node->num_children >= 2)
                analyse_node(ctx, node->children[1]);
            if (node->num_children >= 3)
                analyse_node(ctx, node->children[2]);
            break;
        }

        case NODE_WHILE: {
            if (node->num_children >= 1)
                analyse_expr(ctx, node->children[0]);
            ctx->loop_depth++;
            if (node->num_children >= 2)
                analyse_node(ctx, node->children[1]);
            ctx->loop_depth--;
            break;
        }

        case NODE_FOR: {
            symtable_push(&ctx->symtab);
            if (node->num_children >= 1)
                analyse_node(ctx, node->children[0]);
            if (node->num_children >= 2)
                analyse_expr(ctx, node->children[1]);
            if (node->num_children >= 3)
                analyse_expr(ctx, node->children[2]);
            ctx->loop_depth++;
            if (node->num_children >= 4)
                analyse_node(ctx, node->children[3]);
            ctx->loop_depth--;
            symtable_pop(&ctx->symtab);
            break;
        }

        case NODE_RETURN: {
            if (!ctx->in_func) {
                sem_error(ctx, node->line,
                    "'return' statement outside of a function");
                break;
            }
            SemType expr_type = TY_VOID;
            if (node->num_children > 0)
                expr_type = analyse_expr(ctx, node->children[0]);
            if (!types_compatible(ctx->ret_type, expr_type)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "return type mismatch: function returns '%s', "
                    "expression has type '%s'",
                    semtype_name(ctx->ret_type), semtype_name(expr_type));
                sem_error(ctx, node->line, msg);
            } else if (is_narrowing(ctx->ret_type, expr_type)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "narrowing conversion in return: '%s' -> '%s'",
                    semtype_name(expr_type), semtype_name(ctx->ret_type));
                sem_warn(ctx, node->line, msg);
            }
            break;
        }

        case NODE_EXPR_STMT: {
            if (!strcmp(node->text, "break") ||
                !strcmp(node->text, "continue")) {
                if (ctx->loop_depth == 0) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                        "'%s' outside of a loop", node->text);
                    sem_error(ctx, node->line, msg);
                }
                break;
            }
            if (node->num_children > 0)
                analyse_expr(ctx, node->children[0]);
            break;
        }

        case NODE_ASSIGN:
        case NODE_BINOP:
        case NODE_UNOP:
        case NODE_POSTFIX:
        case NODE_CALL:
            analyse_expr(ctx, node);
            break;

        case NODE_ERROR:
        case NODE_PARAM:
        default:
            break;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  §8  Public entry point
 * ══════════════════════════════════════════════════════════════ */

SemanticResult analyse(AstNode *root) {
    SemCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    symtable_init(&ctx.symtab);
    symtable_push(&ctx.symtab);
    analyse_node(&ctx, root);
    symtable_pop(&ctx.symtab);
    return ctx.result;
}

/* ══════════════════════════════════════════════════════════════
 *  §9  Entry point
 * ══════════════════════════════════════════════════════════════ */

/* Compile with -DSEMANTIC_STANDALONE to build as its own
 * executable. Omit when linking into later stages.       */
#ifdef SEMANTIC_STANDALONE
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source>.hehee\n", argv[0]);
        return 1;
    }

    long  file_size;
    char *source = read_hehee_file(argv[1], &file_size);

    printf("Parsing '%s' ...\n", argv[1]);
    Parser parser;
    parser_init(&parser, source);
    AstNode *ast = parse_program(&parser);

    if (parser.errors > 0) {
        printf("Parser found %d error(s); semantic analysis skipped.\n",
               parser.errors);
        ast_free(ast);
        free(source);
        return 1;
    }
    printf("Parse successful.\n\n");

    printf("Running semantic analysis ...\n");
    SemanticResult res = analyse(ast);

    printf("\n");
    printf("==============================\n");
    printf("  Semantic Analysis Complete\n");
    printf("  Errors   : %d\n", res.errors);
    printf("  Warnings : %d\n", res.warnings);
    printf("==============================\n");

    ast_free(ast);
    free(source);
    return res.errors ? 1 : 0;
}
#endif /* SEMANTIC_STANDALONE */
