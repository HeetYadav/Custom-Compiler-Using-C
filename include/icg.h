/*
 * ============================================================
 *  miniMIPS Compiler — Stage 4: Intermediate Code Generator
 *  Header: Three-Address Code (TAC) instruction set,
 *          operand/instruction structures, and public API.
 *
 *  TAC form
 *  ────────
 *  Every instruction has at most three operands:
 *
 *      result  =  arg1  op  arg2        (binary)
 *      result  =  op  arg1              (unary)
 *      result  =  arg1                  (copy)
 *      param   arg1                     (push call argument)
 *      result  =  call  name  n         (call with n args)
 *      return  arg1                     (function return)
 *      label   L                        (branch target)
 *      goto    L                        (unconditional jump)
 *      if      arg1  goto  L            (conditional jump)
 *      ifFalse arg1  goto  L            (conditional jump)
 *      begin_func  name                 (function prologue)
 *      end_func    name                 (function epilogue)
 *
 *  Temporaries are named  t0, t1, t2, …
 *  Labels      are named  L0, L1, L2, …
 * ============================================================
 */

#ifndef ICG_H
#define ICG_H

#include "semantic.h"   /* pulls in parser.h → lexer.h, SemType, SymTable */

/* ══════════════════════════════════════════════════════════════
 *  TAC Opcodes
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    /* Arithmetic */
    TAC_ADD,        /* result = arg1 + arg2  */
    TAC_SUB,        /* result = arg1 - arg2  */
    TAC_MUL,        /* result = arg1 * arg2  */
    TAC_DIV,        /* result = arg1 / arg2  */
    TAC_MOD,        /* result = arg1 % arg2  */

    /* Unary */
    TAC_NEG,        /* result = -arg1        */
    TAC_NOT,        /* result = !arg1        */
    TAC_BITNOT,     /* result = ~arg1        */

    /* Relational  (result is 0 or 1) */
    TAC_EQ,         /* result = arg1 == arg2 */
    TAC_NEQ,        /* result = arg1 != arg2 */
    TAC_LT,         /* result = arg1 <  arg2 */
    TAC_GT,         /* result = arg1 >  arg2 */
    TAC_LTE,        /* result = arg1 <= arg2 */
    TAC_GTE,        /* result = arg1 >= arg2 */

    /* Logical */
    TAC_AND,        /* result = arg1 && arg2 */
    TAC_OR,         /* result = arg1 || arg2 */

    /* Copy / assignment */
    TAC_COPY,       /* result = arg1         */

    /* Increment / decrement (post/pre) */
    TAC_INC,        /* result = arg1 + 1     */
    TAC_DEC,        /* result = arg1 - 1     */

    /* Control flow */
    TAC_LABEL,      /* label:                */
    TAC_GOTO,       /* goto label            */
    TAC_IF_TRUE,    /* if  arg1 goto label   */
    TAC_IF_FALSE,   /* ifF arg1 goto label   */

    /* Functions */
    TAC_BEGIN_FUNC, /* begin_func name       */
    TAC_END_FUNC,   /* end_func   name       */
    TAC_PARAM,      /* param arg1            */
    TAC_CALL,       /* result = call name n  */
    TAC_RETURN,     /* return arg1  (or void)*/

    TAC_NOP         /* placeholder           */
} TacOp;

/* ══════════════════════════════════════════════════════════════
 *  Operand
 *  Every operand is stored as a string (name, literal, or temp)
 *  plus an optional SemType for later stages.
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    char    name[256];   /* "t0", "42", "x", "L3", ""  */
    SemType type;
} TacOperand;

/* ══════════════════════════════════════════════════════════════
 *  TAC Instruction
 * ══════════════════════════════════════════════════════════════ */

typedef struct TacInstr {
    TacOp        op;
    TacOperand   result;   /* destination / label name / func name */
    TacOperand   arg1;
    TacOperand   arg2;
    int          extra;    /* call: argument count                  */
    int          line;     /* source line for diagnostics           */
    struct TacInstr *next;
} TacInstr;

/* ══════════════════════════════════════════════════════════════
 *  TAC Program  (linked list of instructions)
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    TacInstr *head;        /* first instruction  */
    TacInstr *tail;        /* last  instruction  */
    int       count;       /* total instructions */
} TacProgram;

/* ══════════════════════════════════════════════════════════════
 *  ICG Context  (counters, symbol table re-use)
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    TacProgram  prog;
    SymTable    symtab;    /* re-built during code-gen for type info */
    int         temp_count;
    int         label_count;
} IcgCtx;

/* ══════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════ */

/**
 * Generate TAC for the full AST rooted at `root`.
 * Returns a heap-allocated IcgCtx; free with icg_free().
 * Semantic analysis must have already passed with 0 errors.
 */
IcgCtx *icg_generate(AstNode *root);

/**
 * Print the TAC program to `out` in a human-readable listing.
 * Format:
 *   <index>  <opcode>  <result>  <arg1>  <arg2>
 */
void icg_print(const IcgCtx *ctx, FILE *out);

/**
 * Free all memory owned by an IcgCtx.
 */
void icg_free(IcgCtx *ctx);

#endif /* ICG_H */
