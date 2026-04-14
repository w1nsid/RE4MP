CC ?= gcc
CFLAGS = -O2 -Wall -Wextra

# Default: build for current platform
all: re4mp-server

# Linux build
linux: re4mp-server

re4mp-server: re4mp_server.c
	$(CC) $(CFLAGS) -o $@ $<

# Windows cross-compile (requires MinGW)
windows: re4mp-server.exe

re4mp-server.exe: re4mp_server.c
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ $< -lws2_32

clean:
	rm -f re4mp-server re4mp-server.exe

.PHONY: all linux windows clean
