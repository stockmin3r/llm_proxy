CC=gcc
INC=-Iinclude/
SRC=panda.c
WCC=x86_64-w64-mingw32-gcc
WLINK=-lws2_32

.PHONY:panda
all:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __LINUX__ 1\n#endif" > include/os.h
	gcc $(SRC) -o panda -ggdb

win:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __WINDOWS__ 1\n#endif" > include/os.h
	$(WCC) $(CFLAGS) $(SRC) -m64 -o panda $(WLINK) -ggdb
clean:
	rm -rf panda *.o *~
