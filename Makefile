# Mira compiler build

CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -g -Isrc
LDFLAGS =

SRCS = src/main.c \
       src/error.c \
       src/lexer.c \
       src/ast.c \
       src/parser.c \
       src/typecheck.c \
       src/optimize.c \
       src/value.c \
       src/chunk.c \
       src/compiler.c \
       src/vm.c \
       src/debug.c

OBJS = $(SRCS:.c=.o)
BIN  = mirac

.PHONY: all test clean valgrind

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(BIN)
	./tests/run_tests.sh

valgrind: $(BIN)
	@command -v valgrind >/dev/null 2>&1 || { echo "valgrind not found"; exit 1; }
	@for f in tests/*.mira; do \
		case "$$f" in \
			*_error.*|*_parse_error.*|*_type_*|*_lex_error.*) continue ;; \
		esac; \
		echo "== valgrind $$f =="; \
		valgrind --leak-check=full --error-exitcode=1 ./$(BIN) "$$f" >/dev/null || exit 1; \
	done

clean:
	rm -f $(OBJS) $(BIN)
