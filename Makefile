CC=clang
run: 
	$(CC) -Wall -Wextra -o macwatcher macwatcher.c -lpthread
.PHONY: run
