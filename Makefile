CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcurl -lreadline

comgen: comgen.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

comgen.exe: comgen.c
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ $< -lwinhttp -luser32 -lkernel32 -ladvapi32 -static

clean:
	rm -f comgen

.PHONY: clean
