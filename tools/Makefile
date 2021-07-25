CC ?= gcc
LIBPSXAV_SRC := $(wildcard src/libpsxav/*.c)

all: orgconv.exe sfxconv.exe

orgconv.exe: src/orgconv.c $(LIBPSXAV_SRC)
	$(CC) -g -Og -o $@ $^

sfxconv.exe: src/sfxconv.c $(LIBPSXAV_SRC)
	$(CC) -g -Og -o $@ $^

clean:
	rm -f *.exe

.PHONY: clean
