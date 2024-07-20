CC=gcc
CFLAGS=-I. -lgit2 -pedantic-errors -Wall -std=c11
DEPS=push.c fastforward.c
DESTDIR=/usr/bin

git-watch: git-watch.c $(DEPS)
	$(CC) -o git-watch git-watch.c $(DEPS) $(CFLAGS)

install:
	cp git-watch $(DESTDIR)

clean:
	rm git-watch
