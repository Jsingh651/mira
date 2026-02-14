CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -Isrc

.PHONY: all clean

all:
	@echo "mira: toolchain not linked yet"

clean:
	rm -f mirac src/*.o
