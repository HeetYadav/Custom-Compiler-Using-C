# Stage 3: Semantic Analyser

**Files:** `src/semantic.c`, `include/semantic.h`  
**Build flag:** `-DSEMANTIC_STANDALONE`  
**Input:** `AstNode*` root from the parser  
**Output:** diagnostics on `stderr`, `SemanticResult` (error + warning counts) returned to caller

---

## What It Does

Syntax tells you whether a program is grammatically valid; semantics tells you whether it actually makes sense. The semantic analyser walks the AST and enforces the rules that the parser cannot express in its grammar: declarations before use, type compatibility, correct use of `break`/`continue`, function call arity, and so on.

Every issue found is classified as either an error (which will block code generation) or a warning (which is informational but non-fatal).

---

## Checks Performed

**Scoping and declarations**
- Every identifier used in an expression must have been declared in an enclosing scope before the point of use. Violation → error.
- Declaring the same name twice within the same scope → error.
- Declaring a name that exists in an outer scope → warning (shadowing is legal but flagged).

**Type checking**
- Operand types of binary and unary expressions are resolved bottom-up through the AST.
- Assignment left-hand side and right-hand side must be type-compatible. Incompatible types → error.
- Assigning a floating-point value to an integer variable is a narrowing conversion → warning.
- The expression type in a `return` statement must be compatible with the enclosing function's declared return type. Mismatch → error; narrowing → warning.

**Control flow**
- `break` and `continue` are only valid inside a loop body. Using them outside → error.
- `return` is only valid inside a function body. Using it at file scope → error.

**Function calls**
- The callee must be a symbol declared as a function (`TY_FUNC`). Calling a variable → error.
- The number of arguments in the call must match the number of parameters in the declaration. Mismatch → error.

---

## Symbol Table Implementation

The symbol table is a stack of hash-map scopes:

```c
typedef struct {
    SymScope scopes[128];   // one entry per nesting depth
    int      depth;         // index of the current innermost scope
} SymTable;
```

Each `SymScope` contains 64 hash buckets, and collisions are resolved with separate chaining (`Symbol.next`). The hash function is the standard djb2:

```c
static unsigned hash_name(const char *s) {
    unsigned h = 5381;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h % SYM_HASH_SIZE;
}
```

Scope push/pop happens at block boundaries. When `symtable_pop()` is called, all `Symbol` nodes in the popped scope are freed. Lookup walks from the innermost scope outward, which naturally implements the C scoping rules.

---

## Function Hoisting

Standard C requires functions to be declared before they are called, but many programs define `main` last and have it call helper functions defined earlier. To avoid false "undeclared function" errors when functions are defined in a natural top-down order and mutually recursive patterns exist, the analyser does a two-pass approach at the `NODE_PROGRAM` level:

1. First pass: scan all direct children and register every `NODE_FUNC_DECL` into the global scope as a `TY_FUNC` symbol, recording its return type and parameter count.
2. Second pass: fully analyse each declaration, now with the complete function signature table already available.

This means a function can be called anywhere in the file as long as it is declared at the top level somewhere.

---

## Analyser Context

The entire walk shares a single `SemCtx` that threads through every recursive call:

```c
typedef struct {
    SymTable       symtab;
    SemanticResult result;     // running error/warning counts
    int            loop_depth; // incremented on entry to while/for, decremented on exit
    SemType        ret_type;   // return type of the function currently being visited
    int            in_func;    // 1 when inside a function body, 0 otherwise
} SemCtx;
```

`loop_depth > 0` is the check used to validate `break` and `continue`. `ret_type` is saved and restored on entry/exit of each `NODE_FUNC_DECL`, so nested function calls do not corrupt the outer function's return type context.

---

## Type Resolution

Expression types are resolved in `analyse_expr()`, which returns a `SemType` for every expression node. The rules mirror C's implicit promotion rules in a simplified form:

- A literal `42` resolves to `TY_INT`; `3.14` resolves to `TY_DOUBLE`
- An identifier resolves to its declared type (or its return type if it is a function symbol)
- Binary relational and logical operators always produce `TY_INT` (0 or 1)
- Arithmetic operators produce `TY_DOUBLE` if either operand is `TY_DOUBLE`, `TY_FLOAT` if either is `TY_FLOAT`, otherwise `TY_INT`

---

## Output

```
[SEMANTIC ERROR]   line 8: function 'add' expects 2 argument(s), got 1
[SEMANTIC WARNING] line 3: 'z' shadows outer declaration from line 1

==============================
  Semantic Analysis Complete
  Errors   : 1
  Warnings : 1
==============================
```

On a clean file, the error count is 0 and the ICG is allowed to proceed.
