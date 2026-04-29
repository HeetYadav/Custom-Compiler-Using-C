# miniMIPS Compiler

A four-stage compiler for a C-subset language, written from scratch in C. It takes source files (`.hehee`) through lexical analysis, parsing, semantic validation, and intermediate code generation, producing Three-Address Code (TAC) as its final output.

This project was built as a compiler architecture study — each pipeline stage compiles independently as a standalone binary and also links cleanly into the next stage, making it straightforward to isolate and inspect any individual phase.

---

## Language Supported

The input language is a strict subset of C. It supports:

- Primitive types: `int`, `char`, `float`, `double`, `long`, `short`, `unsigned`, `signed`, `void`
- Function declarations with typed parameters and a body
- Local variable declarations with optional initializers
- Control flow: `if`/`else`, `while`, `for`, `break`, `continue`, `return`
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), relational (`<`, `>`, `<=`, `>=`, `==`, `!=`), logical (`&&`, `||`, `!`), bitwise (`&`, `|`, `^`, `~`, `<<`, `>>`), assignment (`=`, `+=`, `-=`, `*=`, `/=`), prefix/postfix increment and decrement, function calls
- String and character literals
- `struct` keyword is recognized but not deeply analyzed
- Source files must carry the `.hehee` extension

Pointers, arrays, heap allocation, and preprocessor directives are outside the current scope.

---

## Project Structure

```
miniMIPS/
├── src/                  # All .c implementation files
│   ├── lexer.c           # Stage 1 — tokenizer
│   ├── parser.c          # Stage 2 — recursive-descent parser + DOT emitter
│   ├── semantic.c        # Stage 3 — symbol table + type checker
│   ├── icg.c             # Stage 4 — TAC code generator
│   └── fileHandle.c      # Standalone file-reader utility (early prototype)
├── include/              # Public header files
│   ├── lexer.h           # Token types, Lexer struct, lexer API
│   ├── parser.h          # AST node kinds, AstNode struct, parser API
│   ├── semantic.h        # SemType enum, SymTable, SemanticResult, analyser API
│   └── icg.h             # TacOp enum, TacInstr/TacProgram structs, ICG API
├── tests/
│   ├── test.hehee              # Sample source file used for end-to-end testing
│   └── test_expected_icg.txt   # Expected TAC output for test.hehee
├── docs/
│   ├── architecture.md         # Full pipeline walkthrough with data-flow diagram
│   ├── lexer.md                # Stage 1 deep-dive
│   ├── parser.md               # Stage 2 deep-dive
│   ├── semantic.md             # Stage 3 deep-dive
│   └── icg.md                  # Stage 4 deep-dive
├── build/                # Compiled binaries land here (git-ignored)
├── Makefile
└── README.md
```

---

## Building

You need `gcc` and GNU `make`. On Windows, [MSYS2](https://www.msys2.org/) or WSL works well.

```bash
# Build all four stage binaries into build/
make

# Or build a single stage
make build/lexer
make build/parser
make build/semantic
make build/icg
```

Each binary is self-contained — `build/lexer` only depends on `lexer.c`; `build/icg` links all four `.c` files together.

---

## Running

All four binaries accept a single `.hehee` source file as their argument. The `Makefile` provides `run-*` convenience targets that default to `tests/test.hehee`.

```bash
# Run each stage individually against the test file
make run-lexer       # prints a token table to stdout
make run-parser      # writes tests/test.dot, prints parse status
make run-semantic    # prints diagnostics to stderr, summary to stdout
make run-icg         # prints a formatted TAC listing to stdout

# Run all four stages in sequence
make run-all
```

Or invoke the binaries directly:

```bash
./build/lexer    tests/test.hehee
./build/parser   tests/test.hehee [optional_output.dot]
./build/semantic tests/test.hehee
./build/icg      tests/test.hehee
```

The parser writes a [Graphviz](https://graphviz.org/) DOT file. To render the AST as an image:

```bash
dot -Tpng tests/test.dot -o tests/ast.png
```

---

## Pipeline at a Glance

```
source.hehee
     │
     ▼
┌──────────┐   token stream
│  Lexer   │ ─────────────────────────────────────────────────────────────►  (Stage 1 output)
└──────────┘
     │
     ▼
┌──────────┐   AstNode* tree  +  .dot file
│  Parser  │ ─────────────────────────────────────────────────────────────►  (Stage 2 output)
└──────────┘
     │
     ▼
┌──────────────────┐   semantic diagnostics  +  SemanticResult
│ Semantic Analyser│ ────────────────────────────────────────────────────►  (Stage 3 output)
└──────────────────┘
     │
     ▼
┌──────────────────────┐   TacProgram (linked list of TacInstr)
│ Intermediate Code Gen│ ───────────────────────────────────────────────►  (Stage 4 output)
└──────────────────────┘
```

See [`docs/architecture.md`](docs/architecture.md) for a detailed walkthrough of each transition.

---

## Cleaning Up

```bash
make clean
```

This removes all compiled binaries from `build/` and any generated `.dot` or `.png` files under `tests/`.

---

## Docs

- [Architecture & Data Flow](docs/architecture.md)
- [Stage 1: Lexer](docs/lexer.md)
- [Stage 2: Parser](docs/parser.md)
- [Stage 3: Semantic Analyser](docs/semantic.md)
- [Stage 4: Intermediate Code Generator](docs/icg.md)
