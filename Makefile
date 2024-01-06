CC=gcc

.PHONY:panda
all:
	gcc panda.c -o panda -ggdb
clean:
	rm -rf panda *.o
