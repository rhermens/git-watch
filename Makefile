CC=gcc
CFLAGS=-I. -lgit2 -pedantic-errors -Wall -std=c11
DEPS=push.c fastforward.c
DESTDIR=/usr/bin

git-watch: git-watch.c $(DEPS)
	$(CC) -o git-watch git-watch.c $(DEPS) $(CFLAGS)

configure:
	cat git-watch.boilerplate.service | sed "s/%u/$$(whoami)/g" > git-watch.service

install:
	cp git-watch $(DESTDIR)
	cp git-watch.service /etc/systemd/system/git-watch.service

clean:
	rm git-watch
	rm git-watch.service
