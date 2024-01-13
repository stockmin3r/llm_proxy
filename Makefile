CC=gcc
CFLAGS=-pie -fPIE -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now
INC=-Iinclude/
SRC=llm_proxy.c
WCC=x86_64-w64-mingw32-gcc
WLINK=-lws2_32 -lkernel32

.PHONY:panda
all:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __LINUX__ 1\n#endif" > include/os.h
	gcc $(CFLAGS) $(SRC) -o llm_proxy -ggdb

win:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __WINDOWS__ 1\n#endif" > include/os.h
	$(WCC) $(SRC) -m64 -o llm_proxy.exe $(WLINK) -ggdb
clean:
	rm -rf panda *.o *~
