/*
 * ============================================================
 *  miniMIPS Compiler — Stage 4: Intermediate Code Generator
 *
 *  Walks the AST (from parser.c) and emits a flat list of
 *  Three-Address Code (TAC) instructions.
 *
 *  Pipeline:
 *      lexer  →  parser  →  semantic  →  ICG  (this file)
 *
 *  Usage:
 *      ./icg  <source>.hehee
 *
 *  Output:  TAC listing printed to stdout.
 *
 *  Compile (alongside all earlier stages):
 *      gcc -Wall -Wextra -g \
 *          -o icg icg.c semantic.c parser.c lexer.c
 * ============================================================
 */

#include "icg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ══════════════════════════════════════════════════════════════
 *  §1  Forward declarations
 * ══════════════════════════════════════════════════════════════ */

static TacOperand gen_expr(IcgCtx *ctx, AstNode *node);
static void       gen_stmt(IcgCtx *ctx, AstNode *node);
static void       gen_node(IcgCtx *ctx, AstNode *node);

/* ══════════════════════════════════════════════════════════════
 *  §2  Operand / instruction helpers
 * ══════════════════════════════════════════════════════════════ */

/* Build an operand from a name string */
static TacOperand operand(const char *name, SemType type) {
    TacOperand o;
    strncpy(o.name, name ? name : "", 255);
    o.name[255] = '\0';
    o.type = type;
    return o;
}

/* Empty operand (unused slot) */
static TacOperand op_empty(void) { return operand("", TY_UNKNOWN); }

/* Allocate a new temporary  t0, t1, … */
static TacOperand new_temp(IcgCtx *ctx, SemType type) {
    char buf[32];
    snprintf(buf, sizeof(buf), "t%d", ctx->temp_count++);
    return operand(buf, type);
}

/* Allocate a new label  L0, L1, … */
static char *new_label(IcgCtx *ctx) {
    /* caller owns the returned static buffer only until the next call;
     * we copy it into the instruction immediately so that's fine.    */
    static char buf[32];
    snprintf(buf, sizeof(buf), "L%d", ctx->label_count++);
    return buf;
}

/* Append one instruction to the program */
static TacInstr *emit(IcgCtx *ctx, TacOp op,
                       TacOperand result, TacOperand arg1,
                       TacOperand arg2, int extra, int line) {
    TacInstr *ins = calloc(1, sizeof(TacInstr));
    ins->op     = op;
    ins->result = result;
    ins->arg1   = arg1;
    ins->arg2   = arg2;
    ins->extra  = extra;
    ins->line   = line;
    ins->next   = NULL;

    if (!ctx->prog.head) {
        ctx->prog.head = ctx->prog.tail = ins;
    } else {
        ctx->prog.tail->next = ins;
        ctx->prog.tail       = ins;
    }
    ctx->prog.count++;
    return ins;
}

/* Convenience wrappers */
#define EMIT_BIN(ctx,op,r,a,b,ln)  emit(ctx,op,r,a,b,0,ln)
#define EMIT_UNI(ctx,op,r,a,ln)    emit(ctx,op,r,a,op_empty(),0,ln)
#define EMIT_COPY(ctx,r,a,ln)      emit(ctx,TAC_COPY,r,a,op_empty(),0,ln)
#define EMIT_LABEL(ctx,lbl,ln)     emit(ctx,TAC_LABEL,operand(lbl,TY_UNKNOWN),\
                                        op_empty(),op_empty(),0,ln)
#define EMIT_GOTO(ctx,lbl,ln)      emit(ctx,TAC_GOTO,operand(lbl,TY_UNKNOWN),\
                                        op_empty(),op_empty(),0,ln)
#define EMIT_IFTRUE(ctx,cond,lbl,ln) \
    emit(ctx,TAC_IF_TRUE,operand(lbl,TY_UNKNOWN),cond,op_empty(),0,ln)
#define EMIT_IFFALSE(ctx,cond,lbl,ln) \
    emit(ctx,TAC_IF_FALSE,operand(lbl,TY_UNKNOWN),cond,op_empty(),0,ln)

/* ══════════════════════════════════════════════════════════════
 *  §3  Helper: extract name / type from AST node text fields
 *  These mirror the helpers in semantic.c — kept private here.
 * ══════════════════════════════════════════════════════════════ */

static void extract_name(const char *text, char *dst, int cap) {
    const char *p = text;
    while (*p && !isspace((unsigned char)*p)) p++;   /* skip "var"/"func" */
    while (*p &&  isspace((unsigned char)*p)) p++;
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < cap - 1)
        dst[i++] = *p++;
    dst[i] = '\0';
}

static SemType type_from_text(const char *text) {
    return semtype_from_text(text);   /* delegate to semantic.c */
}

/* Map a binary-operator string to its TAC opcode */
static TacOp binop_to_tac(const char *op) {
    if (!strcmp(op, "+"))  return TAC_ADD;
    if (!strcmp(op, "-"))  return TAC_SUB;
    if (!strcmp(op, "*"))  return TAC_MUL;
    if (!strcmp(op, "/"))  return TAC_DIV;
    if (!strcmp(op, "%"))  return TAC_MOD;
    if (!strcmp(op, "==")) return TAC_EQ;
    if (!strcmp(op, "!=")) return TAC_NEQ;
    if (!strcmp(op, "<"))  return TAC_LT;
    if (!strcmp(op, ">"))  return TAC_GT;
    if (!strcmp(op, "<=")) return TAC_LTE;
    if (!strcmp(op, ">=")) return TAC_GTE;
    if (!strcmp(op, "&&")) return TAC_AND;
    if (!strcmp(op, "||")) return TAC_OR;
    return TAC_NOP;
}

/* ══════════════════════════════════════════════════════════════
 *  §4  Expression code generation
 *  Returns the TacOperand that holds the expression's result.
 * ══════════════════════════════════════════════════════════════ */

static TacOperand gen_expr(IcgCtx *ctx, AstNode *node) {
    if (!node) return op_empty();

    switch (node->kind) {

        /* ── literals ── */
        case NODE_INT_LIT:
            return operand(node->text, TY_INT);

        case NODE_FLOAT_LIT:
            return operand(node->text, TY_DOUBLE);

        case NODE_CHAR_LIT:
            return operand(node->text, TY_CHAR);

        case NODE_STR_LIT:
            return operand(node->text, TY_STRING);

        /* ── identifier reference ── */
        case NODE_IDENT: {
            Symbol *sym = symtable_lookup(&ctx->symtab, node->text);
            SemType t   = sym ? sym->type : TY_INT;
            return operand(node->text, t);
        }

        /* ── assignment  x = expr ── */
        case NODE_ASSIGN: {
            if (node->num_children < 2) return op_empty();
            TacOperand rhs = gen_expr(ctx, node->children[1]);
            TacOperand lhs = gen_expr(ctx, node->children[0]);
            /* handle compound assignment operators */
            const char *op = node->text;
            if (!strcmp(op, "=")) {
                EMIT_COPY(ctx, lhs, rhs, node->line);
            } else {
                /* e.g. += → ADD into temp then COPY back */
                TacOp tac_op = TAC_NOP;
                if (!strcmp(op,"+=")) tac_op = TAC_ADD;
                else if (!strcmp(op,"-=")) tac_op = TAC_SUB;
                else if (!strcmp(op,"*=")) tac_op = TAC_MUL;
                else if (!strcmp(op,"/=")) tac_op = TAC_DIV;
                TacOperand tmp = new_temp(ctx, lhs.type);
                EMIT_BIN(ctx, tac_op, tmp, lhs, rhs, node->line);
                EMIT_COPY(ctx, lhs, tmp, node->line);
            }
            return lhs;
        }

        /* ── binary operator ── */
        case NODE_BINOP: {
            if (node->num_children < 2) return op_empty();
            TacOperand l   = gen_expr(ctx, node->children[0]);
            TacOperand r   = gen_expr(ctx, node->children[1]);
            TacOp      op  = binop_to_tac(node->text);
            /* result type: comparisons yield int */
            SemType rtype = (op >= TAC_EQ && op <= TAC_OR) ? TY_INT
                          : (l.type == TY_DOUBLE || r.type == TY_DOUBLE)
                            ? TY_DOUBLE : TY_INT;
            TacOperand tmp = new_temp(ctx, rtype);
            EMIT_BIN(ctx, op, tmp, l, r, node->line);
            return tmp;
        }

        /* ── unary operator ── */
        case NODE_UNOP: {
            if (node->num_children < 1) return op_empty();
            TacOperand val = gen_expr(ctx, node->children[0]);
            TacOp op = TAC_NOP;
            if (!strcmp(node->text, "-"))  op = TAC_NEG;
            else if (!strcmp(node->text, "!"))  op = TAC_NOT;
            else if (!strcmp(node->text, "~"))  op = TAC_BITNOT;
            else if (!strcmp(node->text, "++")) op = TAC_INC;
            else if (!strcmp(node->text, "--")) op = TAC_DEC;
            TacOperand tmp = new_temp(ctx, val.type);
            if (op != TAC_NOP) EMIT_UNI(ctx, op, tmp, val, node->line);
            else               EMIT_COPY(ctx, tmp, val, node->line);
            return tmp;
        }

        /* ── postfix  x++ / x-- ── */
        case NODE_POSTFIX: {
            if (node->num_children < 1) return op_empty();
            TacOperand val = gen_expr(ctx, node->children[0]);
            /* save original value into a temp (postfix returns old value) */
            TacOperand saved = new_temp(ctx, val.type);
            EMIT_COPY(ctx, saved, val, node->line);
            TacOp op = !strcmp(node->text, "++") ? TAC_INC : TAC_DEC;
            TacOperand updated = new_temp(ctx, val.type);
            EMIT_UNI(ctx, op, updated, val, node->line);
            EMIT_COPY(ctx, val, updated, node->line);
            return saved;   /* postfix yields the value before the change */
        }

        /* ── function call ── */
        case NODE_CALL: {
            if (node->num_children < 1) return op_empty();
            AstNode *callee = node->children[0];
            int nargs = node->num_children - 1;

            /* evaluate and push each argument (right-to-left is common
             * in real compilers, but left-to-right is clearer for TAC) */
            for (int i = 1; i <= nargs; i++) {
                TacOperand arg = gen_expr(ctx, node->children[i]);
                emit(ctx, TAC_PARAM,
                     op_empty(), arg, op_empty(), 0, node->line);
            }

            /* look up return type */
            Symbol *sym = symtable_lookup(&ctx->symtab, callee->text);
            SemType rtype = sym ? sym->ret_type : TY_INT;

            TacOperand result = new_temp(ctx, rtype);
            emit(ctx, TAC_CALL,
                 result,
                 operand(callee->text, TY_FUNC),
                 op_empty(),
                 nargs,
                 node->line);
            return result;
        }

        case NODE_ERROR:
        default:
            return op_empty();
    }
}

/* ══════════════════════════════════════════════════════════════
 *  §5  Statement code generation
 * ══════════════════════════════════════════════════════════════ */

static void gen_stmt(IcgCtx *ctx, AstNode *node) {
    if (!node || node->kind == NODE_ERROR) return;

    switch (node->kind) {

        /* ── variable declaration ── */
        case NODE_VAR_DECL: {
            char name[256];
            extract_name(node->text, name, sizeof(name));
            SemType vtype = type_from_text(node->text);

            /* declare into the local symbol table for type queries */
            symtable_declare(&ctx->symtab, name, vtype,
                             TY_UNKNOWN, 0, node->line);

            if (node->num_children > 0) {
                TacOperand rhs = gen_expr(ctx, node->children[0]);
                TacOperand lhs = operand(name, vtype);
                EMIT_COPY(ctx, lhs, rhs, node->line);
            }
            break;
        }

        /* ── block ── */
        case NODE_BLOCK: {
            symtable_push(&ctx->symtab);
            for (int i = 0; i < node->num_children; i++)
                gen_stmt(ctx, node->children[i]);
            symtable_pop(&ctx->symtab);
            break;
        }

        /* ── if / if-else ──
         *
         *  Generated pattern:
         *
         *      <eval cond → tc>
         *      ifFalse tc goto Lelse
         *      <then-body>
         *      goto Lend
         *    Lelse:
         *      <else-body>          (omitted if no else)
         *    Lend:
         */
        case NODE_IF: {
            char lelse[32], lend[32];
            strncpy(lelse, new_label(ctx), 31);
            strncpy(lend,  new_label(ctx), 31);

            /* condition */
            TacOperand cond = gen_expr(ctx, node->children[0]);
            EMIT_IFFALSE(ctx, cond, lelse, node->line);

            /* then branch */
            if (node->num_children >= 2)
                gen_stmt(ctx, node->children[1]);
            EMIT_GOTO(ctx, lend, node->line);

            /* else branch */
            EMIT_LABEL(ctx, lelse, node->line);
            if (node->num_children >= 3)
                gen_stmt(ctx, node->children[2]);

            EMIT_LABEL(ctx, lend, node->line);
            break;
        }

        /* ── while loop ──
         *
         *  Generated pattern:
         *
         *    Lstart:
         *      <eval cond → tc>
         *      ifFalse tc goto Lend
         *      <body>
         *      goto Lstart
         *    Lend:
         */
        case NODE_WHILE: {
            char lstart[32], lend[32];
            strncpy(lstart, new_label(ctx), 31);
            strncpy(lend,   new_label(ctx), 31);

            EMIT_LABEL(ctx, lstart, node->line);
            TacOperand cond = gen_expr(ctx, node->children[0]);
            EMIT_IFFALSE(ctx, cond, lend, node->line);

            if (node->num_children >= 2)
                gen_stmt(ctx, node->children[1]);

            EMIT_GOTO(ctx, lstart, node->line);
            EMIT_LABEL(ctx, lend, node->line);
            break;
        }

        /* ── for loop ──
         *
         *  Generated pattern:
         *
         *      <init>
         *    Lstart:
         *      <eval cond → tc>
         *      ifFalse tc goto Lend
         *      <body>
         *      <post>
         *      goto Lstart
         *    Lend:
         */
        case NODE_FOR: {
            char lstart[32], lend[32];
            strncpy(lstart, new_label(ctx), 31);
            strncpy(lend,   new_label(ctx), 31);

            symtable_push(&ctx->symtab);   /* for-init scope */

            /* init  (child 0) */
            if (node->num_children >= 1)
                gen_stmt(ctx, node->children[0]);

            EMIT_LABEL(ctx, lstart, node->line);

            /* condition  (child 1) */
            if (node->num_children >= 2 &&
                node->children[1]->kind != NODE_EXPR_STMT) {
                TacOperand cond = gen_expr(ctx, node->children[1]);
                EMIT_IFFALSE(ctx, cond, lend, node->line);
            }

            /* body  (child 3) */
            if (node->num_children >= 4)
                gen_stmt(ctx, node->children[3]);

            /* post  (child 2) */
            if (node->num_children >= 3 &&
                node->children[2]->kind != NODE_EXPR_STMT) {
                gen_expr(ctx, node->children[2]);
            }

            EMIT_GOTO(ctx, lstart, node->line);
            EMIT_LABEL(ctx, lend, node->line);

            symtable_pop(&ctx->symtab);
            break;
        }

        /* ── return ── */
        case NODE_RETURN: {
            TacOperand val = op_empty();
            if (node->num_children > 0)
                val = gen_expr(ctx, node->children[0]);
            emit(ctx, TAC_RETURN,
                 op_empty(), val, op_empty(), 0, node->line);
            break;
        }

        /* ── expression statement ── */
        case NODE_EXPR_STMT: {
            if (node->num_children > 0)
                gen_expr(ctx, node->children[0]);
            break;
        }

        /* ── expressions appearing as statements ── */
        case NODE_ASSIGN:
        case NODE_CALL:
        case NODE_UNOP:
        case NODE_POSTFIX:
            gen_expr(ctx, node);
            break;

        default:
            break;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  §6  Top-level node dispatch
 * ══════════════════════════════════════════════════════════════ */

static void gen_node(IcgCtx *ctx, AstNode *node) {
    if (!node || node->kind == NODE_ERROR) return;

    switch (node->kind) {

        case NODE_PROGRAM: {
            /* hoist all function symbols first (same two-pass trick) */
            for (int i = 0; i < node->num_children; i++) {
                AstNode *child = node->children[i];
                if (child->kind != NODE_FUNC_DECL) continue;
                char name[256];
                extract_name(child->text, name, sizeof(name));
                SemType ret = type_from_text(child->text);
                int nparams = (child->num_children > 0)
                              ? child->children[0]->num_children : 0;
                symtable_declare(&ctx->symtab, name,
                                 TY_FUNC, ret, nparams, child->line);
            }
            for (int i = 0; i < node->num_children; i++)
                gen_node(ctx, node->children[i]);
            break;
        }

        case NODE_FUNC_DECL: {
            char name[256];
            extract_name(node->text, name, sizeof(name));
            SemType ret = type_from_text(node->text);

            /* begin_func */
            emit(ctx, TAC_BEGIN_FUNC,
                 operand(name, ret),
                 op_empty(), op_empty(), 0, node->line);

            symtable_push(&ctx->symtab);

            /* declare parameters */
            if (node->num_children >= 1) {
                AstNode *params = node->children[0];
                for (int i = 0; i < params->num_children; i++) {
                    AstNode *param = params->children[i];
                    if (param->kind != NODE_PARAM) continue;
                    char pname[256];
                    extract_name(param->text, pname, sizeof(pname));
                    SemType ptype = type_from_text(param->text);
                    symtable_declare(&ctx->symtab, pname, ptype,
                                     TY_UNKNOWN, 0, param->line);
                }
            }

            /* generate body */
            if (node->num_children >= 2)
                gen_stmt(ctx, node->children[1]);

            symtable_pop(&ctx->symtab);

            /* end_func */
            emit(ctx, TAC_END_FUNC,
                 operand(name, ret),
                 op_empty(), op_empty(), 0, node->line);
            break;
        }

        case NODE_VAR_DECL:
            gen_stmt(ctx, node);
            break;

        default:
            break;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  §7  Public API implementation
 * ══════════════════════════════════════════════════════════════ */

IcgCtx *icg_generate(AstNode *root) {
    IcgCtx *ctx = calloc(1, sizeof(IcgCtx));
    symtable_init(&ctx->symtab);
    symtable_push(&ctx->symtab);   /* global scope */

    gen_node(ctx, root);

    symtable_pop(&ctx->symtab);
    return ctx;
}

void icg_free(IcgCtx *ctx) {
    if (!ctx) return;
    TacInstr *ins = ctx->prog.head;
    while (ins) {
        TacInstr *nx = ins->next;
        free(ins);
        ins = nx;
    }
    /* free any remaining symbol-table scopes */
    while (ctx->symtab.depth >= 0)
        symtable_pop(&ctx->symtab);
    free(ctx);
}

/* ══════════════════════════════════════════════════════════════
 *  §8  TAC printer
 * ══════════════════════════════════════════════════════════════ */

/* Return a short string for each opcode */
static const char *tac_op_name(TacOp op) {
    switch (op) {
        case TAC_ADD:        return "ADD";
        case TAC_SUB:        return "SUB";
        case TAC_MUL:        return "MUL";
        case TAC_DIV:        return "DIV";
        case TAC_MOD:        return "MOD";
        case TAC_NEG:        return "NEG";
        case TAC_NOT:        return "NOT";
        case TAC_BITNOT:     return "BITNOT";
        case TAC_EQ:         return "EQ";
        case TAC_NEQ:        return "NEQ";
        case TAC_LT:         return "LT";
        case TAC_GT:         return "GT";
        case TAC_LTE:        return "LTE";
        case TAC_GTE:        return "GTE";
        case TAC_AND:        return "AND";
        case TAC_OR:         return "OR";
        case TAC_COPY:       return "COPY";
        case TAC_INC:        return "INC";
        case TAC_DEC:        return "DEC";
        case TAC_LABEL:      return "LABEL";
        case TAC_GOTO:       return "GOTO";
        case TAC_IF_TRUE:    return "IF_TRUE";
        case TAC_IF_FALSE:   return "IF_FALSE";
        case TAC_BEGIN_FUNC: return "BEGIN_FUNC";
        case TAC_END_FUNC:   return "END_FUNC";
        case TAC_PARAM:      return "PARAM";
        case TAC_CALL:       return "CALL";
        case TAC_RETURN:     return "RETURN";
        default:             return "NOP";
    }
}

void icg_print(const IcgCtx *ctx, FILE *out) {
    fprintf(out, "\n");
    fprintf(out, "══════════════════════════════════════════════════\n");
    fprintf(out, "  Three-Address Code  (%d instructions)\n",
            ctx->prog.count);
    fprintf(out, "══════════════════════════════════════════════════\n");
    fprintf(out, "  %-4s  %-12s  %-10s  %-10s  %-10s\n",
            "IDX", "OPCODE", "RESULT", "ARG1", "ARG2");
    fprintf(out, "  %-4s  %-12s  %-10s  %-10s  %-10s\n",
            "---", "------", "------", "----", "----");

    int idx = 0;
    for (TacInstr *ins = ctx->prog.head; ins; ins = ins->next, idx++) {

        /* Special formatting for label lines */
        if (ins->op == TAC_LABEL) {
            fprintf(out, "\n  %s:\n", ins->result.name);
            continue;
        }

        /* Special formatting for function boundaries */
        if (ins->op == TAC_BEGIN_FUNC) {
            fprintf(out,
                "\n  ┌─ begin_func  %s ─────────────────────────\n",
                ins->result.name);
            continue;
        }
        if (ins->op == TAC_END_FUNC) {
            fprintf(out,
                "  └─ end_func    %s ─────────────────────────\n\n",
                ins->result.name);
            continue;
        }

        /* Normal instruction row */
        const char *r  = ins->result.name[0] ? ins->result.name : "-";
        const char *a1 = ins->arg1.name[0]   ? ins->arg1.name   : "-";
        const char *a2 = ins->arg2.name[0]   ? ins->arg2.name   : "-";

        /* CALL: show "name / n args" in the arg1 column */
        if (ins->op == TAC_CALL) {
            char call_info[300];
            snprintf(call_info, sizeof(call_info),
                     "%s/%dargs", ins->arg1.name, ins->extra);
            fprintf(out, "  %-4d  %-12s  %-10s  %-10s  %-10s\n",
                    idx, tac_op_name(ins->op), r, call_info, a2);
            continue;
        }

        fprintf(out, "  %-4d  %-12s  %-10s  %-10s  %-10s\n",
                idx, tac_op_name(ins->op), r, a1, a2);
    }
    fprintf(out, "══════════════════════════════════════════════════\n");
}

/* ══════════════════════════════════════════════════════════════
 *  §9  Entry point
 * ══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source>.hehee\n", argv[0]);
        return 1;
    }

    /* ── read source ── */
    long  file_size;
    char *source = read_hehee_file(argv[1], &file_size);

    /* ── parse ── */
    printf("Parsing '%s' ...\n", argv[1]);
    Parser parser;
    parser_init(&parser, source);
    AstNode *ast = parse_program(&parser);

    if (parser.errors > 0) {
        fprintf(stderr,
            "Parser found %d error(s); stopping.\n", parser.errors);
        ast_free(ast); free(source); return 1;
    }
    printf("Parse successful.\n");

    /* ── semantic analysis ── */
    printf("Running semantic analysis ...\n");
    SemanticResult sem = analyse(ast);
    if (sem.errors > 0) {
        fprintf(stderr,
            "Semantic analysis found %d error(s); stopping.\n",
            sem.errors);
        ast_free(ast); free(source); return 1;
    }
    if (sem.warnings > 0)
        printf("Semantic analysis: %d warning(s).\n", sem.warnings);
    printf("Semantic analysis passed.\n");

    /* ── intermediate code generation ── */
    printf("Generating intermediate code ...\n");
    IcgCtx *icg = icg_generate(ast);

    /* ── build output path: replace .hehee with .icg ── */
    char icg_path[512];
    strncpy(icg_path, argv[1], 511); icg_path[511] = '\0';
    char *ext = strrchr(icg_path, '.');
    if (ext) strcpy(ext, ".icg");
    else     strncat(icg_path, ".icg", 511 - strlen(icg_path));

    /* ── write TAC to file ── */
    FILE *icg_file = fopen(icg_path, "w");
    if (!icg_file) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", icg_path);
        icg_free(icg); ast_free(ast); free(source); return 1;
    }
    icg_print(icg, icg_file);
    fclose(icg_file);

    printf("TAC written  →  %s\n", icg_path);

    /* ── cleanup ── */
    icg_free(icg);
    ast_free(ast);
    free(source);
    return 0;
}
