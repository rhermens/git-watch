CC=g++

git-watch: git-watch.cpp
	$(CC) -o git-watch git-watch.cpp -I. -lgit2
