CC=gcc
CFLAGS=-pie -fPIE -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now
LDFLAGS=-lcurl
INC=-Iinclude/
I2=-I/usr/include/x86_64-linux-gnu/
SRC=llm_proxy.c http.c windows.c lib.c
WCC=x86_64-w64-mingw32-g++
WLINK=-lws2_32 -lkernel32 -lmsvcrt -Llibcurl-x64

.PHONY:panda
all:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __LINUX__ 1\n#endif" > include/os.h
	gcc $(CFLAGS) $(INC) $(SRC) -o llm_proxy $(LDFLAGS) -ggdb
	cp llm_proxy bin/

win:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __WINDOWS__ 1\n#endif" > include/os.h
	$(WCC) $(INC) $(SRC) -m64 -o llm_proxy.exe $(WLINK) -ggdb
clean:
	rm -rf panda *.o *~
win2:
	$(WCC) w.c -m64 -o w.exe $(WLINK) -ggdb

dep:
	curl -k https://github.com/indygreg/python-build-standalone/releases/download/20240107/cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst -o cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst
	unzstd cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst
	mv cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst bin/python

proc:
	$(WCC) proc.c  -m64 -o proc.exe $(WLINK) -ggdb
	$(WCC) child.c -m64 -o child.exe $(WLINK) -ggdb

