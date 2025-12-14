CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lcurl -lreadline

comgen: comgen.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f comgen

.PHONY: clean
