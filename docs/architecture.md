# Compiler Architecture & Data Flow

This document traces a `.hehee` source file through all four compilation stages, explaining what data each stage receives, what it produces, and exactly how those structures are handed to the next phase.

---

## The Four Stages

```
.hehee source text
        │
        │  (char*)
        ▼
   ┌─────────┐
   │  Lexer  │          lexer.c / lexer.h
   └─────────┘
        │
        │  Token stream  (one Token at a time, on demand)
        ▼
   ┌──────────┐
   │  Parser  │          parser.c / parser.h
   └──────────┘
        │
        │  AstNode* (heap tree)  +  test.dot (Graphviz)
        ▼
   ┌───────────────────┐
   │ Semantic Analyser │   semantic.c / semantic.h
   └───────────────────┘
        │
        │  SemanticResult (error/warning counts, diagnostics on stderr)
        ▼
   ┌─────────────────────────┐
   │ Intermediate Code Gen   │  icg.c / icg.h
   └─────────────────────────┘
        │
        │  TacProgram (linked list of TacInstr)
        ▼
   TAC listing printed to stdout
```

---

## Stage 1 → Stage 2: Lexer to Parser

The lexer does not tokenize the entire file upfront. Instead it exposes a single function, `next_token()`, which the parser calls every time it needs the next token. This is the classic **lazy / on-demand** tokenization pattern — only one `Token` struct is live at any moment, keeping memory usage flat.

The `Parser` struct embeds a `Lexer` and holds exactly one look-ahead token (`current`). When the parser calls `eat()`, it consumes `current` and calls `next_token()` to refill it. This single-token lookahead is sufficient to drive the entire LL(1)-style recursive-descent grammar.

---

## Stage 2 → Stage 3: Parser to Semantic Analyser

After `parse_program()` returns, the caller has an `AstNode*` pointing to the root `NODE_PROGRAM` node. The entire program is represented as a heap-allocated tree: each `AstNode` contains its kind, a `text` label (used to encode names, operator symbols, and type annotations), a source line number, and an array of child pointers (up to 64).

The semantic analyser receives this tree directly and walks it recursively via `analyse_node()`. It never re-tokenizes or re-parses anything — the AST is the sole input. As it walks, it builds its own `SymTable` (separate from any table the parser might maintain) so that type information can be resolved independently.

---

## Stage 3 → Stage 4: Semantic Analyser to ICG

The semantic analyser returns a `SemanticResult` — a simple struct containing error and warning counts. The ICG is only invoked if `result.errors == 0`. This is enforced in `icg.c`'s `main()`: if the semantic stage reports any errors, the binary exits rather than generating potentially broken TAC.

The ICG then re-walks the same `AstNode*` tree using its own recursive visitor (`icg_node()`), rebuilding a lightweight symbol table as it goes for type annotation purposes. It emits `TacInstr` nodes into a `TacProgram` (a singly-linked list) and finally prints the formatted listing.

---

## Key Data Structures

### Token

```c
typedef struct {
    TokenType type;    // enum: TOK_INT, TOK_IDENTIFIER, TOK_PLUS, etc.
    char      value[256];  // raw lexeme text
    int       line;        // source line number
} Token;
```

### AstNode

```c
typedef struct AstNode {
    NodeKind       kind;             // what grammatical construct this is
    char           text[256];        // label: name, op, or type annotation
    int            line;
    int            num_children;
    struct AstNode *children[64];   // child sub-trees
} AstNode;
```

### Symbol

```c
typedef struct Symbol {
    char          name[256];
    SemType       type;        // TY_INT, TY_FUNC, etc.
    SemType       ret_type;    // for functions: their return type
    int           param_count; // for functions: expected argument count
    int           decl_line;
    struct Symbol *next;       // hash-chain within a scope bucket
} Symbol;
```

### TacInstr

```c
typedef struct TacInstr {
    TacOp      op;       // e.g. TAC_ADD, TAC_GOTO, TAC_CALL
    TacOperand result;   // destination operand (temp name, label, etc.)
    TacOperand arg1;
    TacOperand arg2;
    int        extra;    // for TAC_CALL: argument count
    int        line;
    struct TacInstr *next;
} TacInstr;
```

---

## End-to-End Example

Given the input `tests/test.hehee`:

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 5;
    int y = 10;
    int z = add(x, y);
    if (z > 10) {
        z = z - 1;
    } else {
        z = z + 1;
    }
    while (x > 0) {
        x = x - 1;
    }
    return 0;
}
```

The lexer produces tokens like `INT[int]`, `IDENT[add]`, `LPAREN`, `INT[int]`, `IDENT[a]`, etc.

The parser produces a tree rooted at `NODE_PROGRAM` with two `NODE_FUNC_DECL` children (`add` and `main`). Inside `main`'s body block you get `NODE_VAR_DECL` nodes for `x`, `y`, `z`, a `NODE_IF` with a `>` binop condition, and a `NODE_WHILE` loop.

The semantic analyser hoists `add` and `main` into the global scope, verifies `add(x, y)` matches the two-parameter signature, checks the `>` and `-` operand types, and exits with 0 errors.

The ICG then walks the tree and emits 29 TAC instructions, including temporaries `t0`–`t6`, labels `L0`–`L3`, and function prologue/epilogue markers. The final listing is shown in `tests/test_expected_icg.txt`.
