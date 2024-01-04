CC=gcc

.PHONY:panda
all:
	gcc panda.c -o panda
clean:
	rm -rf panda *.o
