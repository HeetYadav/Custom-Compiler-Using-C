# Stage 4: Intermediate Code Generator

**Files:** `src/icg.c`, `include/icg.h`  
**Build flag:** (none — always links all four `.c` files together)  
**Input:** `AstNode*` root (after semantic analysis passes with 0 errors)  
**Output:** a formatted Three-Address Code (TAC) listing printed to stdout

---

## What It Does

After the semantic analyser confirms the program is well-typed and structurally correct, the ICG translates the AST into a flat, linear sequence of simple instructions — each with at most three named operands. This representation, Three-Address Code, is the standard bridge between the high-level AST and low-level machine code or further optimization passes.

In this compiler, TAC is the final output. A real backend would then map TAC instructions to target-architecture assembly (MIPS, x86, ARM), but that stage is not implemented here.

---

## TAC Form

Every instruction in the TAC program has the general shape:

```
result  =  arg1  op  arg2     (binary operation)
result  =  op  arg1           (unary operation)
result  =  arg1               (copy)
param   arg1                  (push argument before a call)
result  =  call  name  n      (call function with n args)
return  arg1                  (return a value)
label:                        (branch target)
goto    L                     (unconditional jump)
if      arg1 goto L           (conditional jump)
ifFalse arg1 goto L           (conditional jump)
begin_func  name              (function prologue marker)
end_func    name              (function epilogue marker)
```

Temporaries are named `t0`, `t1`, `t2`, … and are allocated by the ICG as needed. Jump labels are named `L0`, `L1`, `L2`, …

---

## Implementation

The ICG maintains an `IcgCtx`:

```c
typedef struct {
    TacProgram  prog;        // the output — head/tail/count of TacInstr list
    SymTable    symtab;      // rebuilt locally for type annotation
    int         temp_count;  // next temporary index
    int         label_count; // next label index
} IcgCtx;
```

The core of the generator is a recursive function `icg_node()` (for statements/declarations) and `icg_expr()` (for expressions). `icg_expr()` always returns the name of the operand holding the expression's result — either a variable name or a freshly allocated temporary.

For example, `a + b` in the AST is two `NODE_IDENT` children under a `NODE_BINOP("+")`. The ICG calls `icg_expr()` on each child (which return `"a"` and `"b"` directly since they are identifiers), then emits:

```
t0 = a + b
```

and returns `"t0"` to the caller.

---

## Control Flow Translation

The most interesting part of TAC generation is how structured control flow (`if`/`else`, `while`, `for`) becomes flat conditional jumps.

**if/else:**

```
  evaluate condition → t2
  ifFalse t2 goto L0
  <then-body instructions>
  goto L1
L0:
  <else-body instructions>
L1:
```

**while:**

```
L2:
  evaluate condition → t5
  ifFalse t5 goto L3
  <body instructions>
  goto L2
L3:
```

Labels are allocated in sequence so they never collide across nested control structures.

---

## Function Calls

Before emitting a `TAC_CALL`, the ICG iterates over the argument expressions left to right, emitting one `TAC_PARAM` per argument:

```
param x
param y
t1 = call add/2args
```

The `/2args` suffix in the call operand encodes the argument count inline in the string, which the printer then displays as `call add/2args`. A real backend would use this count to set up the stack frame.

---

## Output Format

`icg_print()` produces a formatted listing with function boundaries clearly marked:

```
══════════════════════════════════════════════════
  Three-Address Code  (29 instructions)
══════════════════════════════════════════════════
  IDX   OPCODE        RESULT      ARG1        ARG2

  ┌─ begin_func  add ─────────────────────────
  1     ADD           t0          a           b
  2     RETURN        -           t0          -
  └─ end_func    add ─────────────────────────

  ┌─ begin_func  main ─────────────────────────
  5     COPY          x           5           -
  6     COPY          y           10          -
  7     PARAM         -           x           -
  8     PARAM         -           y           -
  9     CALL          t1          add/2args   -
  ...
```

Empty operand slots are printed as `-` for clarity. Labels appear on their own lines without an index number.

---

## Relation to a Real Backend

The TAC produced here is machine-independent. To target, say, MIPS32:
- `TAC_ADD` would map to the MIPS `add` or `addu` instruction.
- Temporaries `t0`–`tN` would be assigned to MIPS registers `$t0`–`$t9` or spilled to the stack if there are more than 8.
- `TAC_CALL` would become a `jal` (jump-and-link) with arguments in `$a0`–`$a3`.
- `TAC_GOTO` and `TAC_IF_FALSE` would become `j` and `beq`/`bne` respectively.

The name `miniMIPS` reflects this intended target, even though the MIPS emission stage is out of scope for the current implementation.
