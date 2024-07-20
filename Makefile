CC=gcc

git-watch: git-watch.c
	$(CC) -o git-watch git-watch.c -I. -lgit2 -pedantic-errors -Wall -std=c11
