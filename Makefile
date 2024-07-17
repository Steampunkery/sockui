CC=gcc
CFLAGS=-Wall -Werror -Wno-unused-function -Wextra -g3 -fsanitize=undefined -fno-sanitize-recover -D_GNU_SOURCE

default: libsockui.a

sockui.o: sockui.c sockui.h
	$(CC) -c -o $@ $< $(CFLAGS)

libsockui.a: sockui.o
	ar rcs $@ $^

demo: demo.c sockui.o
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f demo *.o *.a
