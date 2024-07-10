CC=gcc
CFLAGS=-Wall -Werror -Wno-unused-function -Wextra -g3 -fsanitize=undefined -fno-sanitize-recover -O0 -std=gnu11 -D_GNU_SOURCE

default: sockui

sockui: main.c
	$(CC) -o $@ $< $(CFLAGS)

.PHONY: clean

clean:
	rm -f sockui *.o
