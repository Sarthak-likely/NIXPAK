CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

SRC = core.c json.c tui.c drm.c font8x16.c input.c ui.c main_fb.c main.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean install uninstall

all: nixpkg

nixpkg: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c common.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: nixpkg
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 nixpkg $(DESTDIR)$(BINDIR)/nixpkg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/nixpkg

clean:
	rm -f $(OBJ) nixpkg
