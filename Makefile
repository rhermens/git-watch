CC=gcc
CFLAGS=-I. -lgit2 -pedantic-errors -Wall -std=c11
DEPS=push.c fastforward.c

git-watch: git-watch.c $(DEPS)
	$(CC) -o git-watch git-watch.c $(DEPS) $(CFLAGS)

clean:
	rm git-watch
