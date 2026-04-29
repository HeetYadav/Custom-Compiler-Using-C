# Stage 1: Lexer

**Files:** `src/lexer.c`, `include/lexer.h`  
**Build flag:** `-DLEXER_STANDALONE`  
**Input:** a `.hehee` source file path  
**Output:** a formatted token table printed to stdout

---

## What It Does

The lexer (also called a tokenizer or scanner) is the first contact point with the raw source text. Its job is to break the character stream into a flat sequence of classified tokens — discarding whitespace and comments along the way.

Every subsequent stage depends on the lexer, but none of them read raw characters. Once the lexer is in play, the rest of the compiler thinks in terms of `Token` values rather than individual characters.

---

## How It Works

The central state is the `Lexer` struct:

```c
typedef struct {
    const char *src;   // pointer to the null-terminated source buffer
    int         pos;   // current read position (byte index)
    int         line;  // current source line number (1-indexed)
} Lexer;
```

`next_token()` is the workhorse. Each call advances `pos` through the source and returns exactly one `Token`. The logic follows a predictable priority order:

1. **Skip whitespace and comments.** Newlines increment `line`. Single-line (`//`) and multi-line (`/* ... */`) comments are consumed entirely before the next meaningful character is examined.

2. **Identifiers and keywords.** If the current character is a letter or underscore, the lexer reads a full word into a buffer and then does a linear scan of the keyword table. If the word matches a keyword (e.g. `while`, `return`, `struct`), the corresponding keyword token type is returned. Otherwise it becomes a `TOK_IDENTIFIER`.

3. **Numeric literals.** A digit starts a number. The lexer grabs all consecutive digits; if a `.` follows, it continues consuming digits and returns `TOK_FLOAT_LITERAL`. Otherwise it returns `TOK_INT_LITERAL`.

4. **String literals.** A `"` triggers string mode — everything until the closing `"` is captured, with backslash sequences preserved as-is.

5. **Character literals.** Similar to strings but bounded by single quotes.

6. **Operators and delimiters.** A `switch` on the current character handles all multi-character operators by peeking one character ahead (`peek2()`). For example, seeing `<` causes a peek: if the next char is `=`, it returns `TOK_LTE` (`<=`); if it is `<`, it returns `TOK_LSHIFT` (`<<`); otherwise just `TOK_LT`.

The internal helpers `peek()`, `peek2()`, and `advance()` are tiny inlined functions that keep the character-access logic readable:

```c
static char peek(Lexer *lex)    { return lex->src[lex->pos]; }
static char peek2(Lexer *lex)   { return lex->src[lex->pos + 1]; }
static char advance(Lexer *lex) { return lex->src[lex->pos++]; }
```

---

## Token Structure

```c
typedef struct {
    TokenType type;       // enumerated token category
    char      value[256]; // the raw source text of the token
    int       line;       // source line where the token appears
} Token;
```

`TokenType` is an enum with 70+ members covering all keywords, literals, operators, delimiters, and the sentinel values `TOK_EOF` and `TOK_UNKNOWN`.

---

## File Reading

The lexer includes `read_hehee_file()`, a simple utility that validates the `.hehee` extension before opening the file. It reads the entire source into a heap buffer (null-terminated) and returns it. This buffer is then passed to `lexer_init()`. The caller is responsible for `free()`-ing it after the entire pipeline completes.

---

## Standalone Output

When built with `-DLEXER_STANDALONE` and run against a source file, the binary calls `print_tokens_inline()` on the full source, which prints the token stream in a compact format:

```
Tokens: INT → IDENT[add] → LPAREN → INT → IDENT[a] → COMMA → INT → IDENT[b] → RPAREN → ...
```

`dump_token_table()` is also available and produces a three-column table (LINE | TYPE | VALUE) — useful for systematic inspection of a token stream.

---

## Example

Running `./build/lexer tests/test.hehee` against:

```c
int add(int a, int b) {
    return a + b;
}
```

produces tokens beginning with:

```
LINE   TYPE             VALUE
----   ----             -----
1      INT              int
1      IDENT            add
1      LPAREN           (
1      INT              int
1      IDENT            a
1      COMMA            ,
1      INT              int
1      IDENT            b
1      RPAREN           )
1      LBRACE           {
2      RETURN           return
...
```
