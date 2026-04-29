CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
SRCDIR  = src
INCDIR  = include
BINDIR  = build
INPUT   = tests/test.hehee

# ── default: build all four stages ────────────────────────────
all: $(BINDIR)/lexer $(BINDIR)/parser $(BINDIR)/semantic $(BINDIR)/icg

$(BINDIR):
	mkdir -p $(BINDIR)

# ── Stage 1: lexer ────────────────────────────────────────────
$(BINDIR)/lexer: $(SRCDIR)/lexer.c $(INCDIR)/lexer.h | $(BINDIR)
	$(CC) $(CFLAGS) -DLEXER_STANDALONE -o $@ $(SRCDIR)/lexer.c

# ── Stage 2: parser ───────────────────────────────────────────
$(BINDIR)/parser: $(SRCDIR)/parser.c $(SRCDIR)/lexer.c $(INCDIR)/lexer.h $(INCDIR)/parser.h | $(BINDIR)
	$(CC) $(CFLAGS) -DPARSER_STANDALONE -o $@ $(SRCDIR)/parser.c $(SRCDIR)/lexer.c

# ── Stage 3: semantic analyser ────────────────────────────────
$(BINDIR)/semantic: $(SRCDIR)/semantic.c $(SRCDIR)/parser.c $(SRCDIR)/lexer.c \
                    $(INCDIR)/lexer.h $(INCDIR)/parser.h $(INCDIR)/semantic.h | $(BINDIR)
	$(CC) $(CFLAGS) -DSEMANTIC_STANDALONE \
	    -o $@ $(SRCDIR)/semantic.c $(SRCDIR)/parser.c $(SRCDIR)/lexer.c

# ── Stage 4: intermediate code generator ──────────────────────
$(BINDIR)/icg: $(SRCDIR)/icg.c $(SRCDIR)/semantic.c $(SRCDIR)/parser.c $(SRCDIR)/lexer.c \
               $(INCDIR)/lexer.h $(INCDIR)/parser.h $(INCDIR)/semantic.h $(INCDIR)/icg.h | $(BINDIR)
	$(CC) $(CFLAGS) \
	    -o $@ $(SRCDIR)/icg.c $(SRCDIR)/semantic.c $(SRCDIR)/parser.c $(SRCDIR)/lexer.c

# ── convenience run targets ───────────────────────────────────
run-lexer: $(BINDIR)/lexer
	./$(BINDIR)/lexer $(INPUT)

run-parser: $(BINDIR)/parser
	./$(BINDIR)/parser $(INPUT)
	@echo "Render: dot -Tpng tests/test.dot -o tests/ast.png"

run-semantic: $(BINDIR)/semantic
	./$(BINDIR)/semantic $(INPUT)

run-icg: $(BINDIR)/icg
	./$(BINDIR)/icg $(INPUT)

run-all: run-lexer run-parser run-semantic run-icg

clean:
	rm -f $(BINDIR)/lexer $(BINDIR)/parser $(BINDIR)/semantic $(BINDIR)/icg
	rm -f tests/*.dot tests/*.png

.PHONY: all clean run-lexer run-parser run-semantic run-icg run-all
