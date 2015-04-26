CC=gcc
CFLAGS=-pedantic -Wall -pthread

shell: shell.c shell.h
	$(CC) $(CFLAGS) shell.c -o shell

.PHONY: clean

clean:
	rm -f shell
