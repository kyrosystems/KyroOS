CC = gcc
AS = nasm
LD = ld

CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
         -Ilib/libc -Ilib/kyroos_gfx -Ilib/tui -Ilib -I../src/include
LDFLAGS = -T linker.ld -nostdlib

# Find all app directories
APPS = $(patsubst %/main.c,%,$(wildcard */main.c))
BIN_DIR = ../build/isodir/bin

# Library objects
LIBC_OBJS = lib/libc/chk.o lib/libc/errno.o lib/libc/print.o lib/libc/puts.o \
            lib/libc/stdlib.o lib/libc/string.o lib/libc/strlen.o lib/libc/dns.o
GFX_OBJS  = $(patsubst %.c,%.o,$(wildcard lib/kyroos_gfx/*.c))
TUI_OBJS  = $(patsubst %.c,%.o,$(wildcard lib/tui/*.c))
LIB_OBJS  = $(LIBC_OBJS) $(GFX_OBJS) $(TUI_OBJS)

.PHONY: all clean $(APPS) libs

all: libs $(APPS)

libs: $(LIB_OBJS)

%.o: %.c
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(APPS):
	@mkdir -p $(BIN_DIR)
	@echo "Building userspace app: $@"
	@$(AS) -f elf64 crt0.asm -o $@_crt0.o
	@for f in $@/*.c; do \
		$(CC) $(CFLAGS) -c $$f -o $${f%.c}.o; \
	done
	@$(LD) $(LDFLAGS) -o $(BIN_DIR)/$@ $@_crt0.o $@/*.o $(LIB_OBJS)
	@rm -f $@/*.o $@_crt0.o

clean:
	@rm -rf $(BIN_DIR)/*
	@rm -f $(LIB_OBJS) *.o */*.o lib/*/*.o
