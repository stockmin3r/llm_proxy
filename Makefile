# Compiler and flags
CC=gcc
CFLAGS=-pie -fPIE -D_FORTIFY_SOURCE=2 -Wl,-z,relro,-z,now
LDFLAGS=-lcurl

# Include directories
INC=-Iinclude/
I2=-I/usr/include/x86_64-linux-gnu/

# Source files
SRC=llm_proxy.c http.c windows.c lib.c

# Windows-specific compiler and linker flags
WCC=x86_64-w64-mingw32-g++
WLINK=-lws2_32 -lkernel32 -lmsvcrt -Llibcurl-x64

# Target for building the llm_proxy executable
.PHONY:panda
all:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __LINUX__ 1\n#endif" > include/os.h
	gcc $(CFLAGS) $(INC) $(SRC) -o llm_proxy $(LDFLAGS) -ggdb
	cp llm_proxy bin/

# Target for building the llm_proxy executable for Windows
win:
	@/usr/bin/echo -e "#ifndef __OS_H\n#define __OS_H\n#define __WINDOWS__ 1\n#endif" > include/os.h
	$(WCC) $(INC) $(SRC) -m64 -o llm_proxy.exe $(WLINK) -ggdb

# Target for cleaning up build artifacts
clean:
	rm -rf panda *.o *~

# Target for building the w.exe executable for Windows
win2:
	$(WCC) w.c -m64 -o w.exe $(WLINK) -ggdb

# Target for downloading and extracting a Python distribution
dep:
	curl -k https://github.com/indygreg/python-build-standalone/releases/download/20240107/cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst -o cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst
	unzstd cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst
	mv cpython-3.10.13+20240107-aarch64-unknown-linux-gnu-lto-full.tar.zst bin/python

# Target for building the proc.exe and child.exe executables for Windows
proc:
	$(WCC) proc.c  -m64 -o proc.exe $(WLINK) -ggdb
	$(WCC) child.c -m64 -o child.exe $(WLINK) -ggdb
