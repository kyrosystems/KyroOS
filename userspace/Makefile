CC = gcc
AS = nasm
LD = ld

CFLAGS = -O2 -mno-red-zone -Wall -Wextra -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
         -Ilib/libc -Ilib/kyroos_gfx -Ilib/tui -Ilib/libgl -Ilib/libgl/include -Ilib/libvulkan -Ilib/libtls -Ilib -I../src/include -I../limine
LDFLAGS = -T linker.ld -nostdlib

# Find all app directories
APPS = $(patsubst %/main.c,%,$(wildcard */main.c))
BIN_DIR = ../build/isodir/bin

# Library objects
LIBC_OBJS = lib/libc/chk.o lib/libc/errno.o lib/libc/print.o lib/libc/puts.o \
            lib/libc/stdlib.o lib/libc/string.o lib/libc/strlen.o lib/libc/dns.o lib/libc/math.o lib/libc/stdio.o
GFX_OBJS  = $(patsubst %.c,%.o,$(wildcard lib/kyroos_gfx/*.c))
TUI_OBJS  = $(patsubst %.c,%.o,$(wildcard lib/tui/*.c))
GL_OBJS   = $(patsubst %.c,%.o,$(wildcard lib/libgl/*.c))
VK_OBJS   = $(patsubst %.c,%.o,$(wildcard lib/libvulkan/*.c))
TLS_OBJS  = $(patsubst %.c,%.o,$(wildcard lib/libtls/*.c))
LIB_OBJS  = $(LIBC_OBJS) $(GFX_OBJS) $(TUI_OBJS) $(GL_OBJS) $(VK_OBJS) $(TLS_OBJS)

.PHONY: all clean $(APPS) libs

all: libs $(APPS)

libs: $(LIB_OBJS)

%.o: %.c $(wildcard lib/libc/*.h lib/kyroos_gfx/*.h ../src/include/*.h)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(APPS): $(LIB_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Building userspace app: $@"
	@$(AS) -f elf64 crt0.asm -o $@_crt0.o
	@set -e; for f in $@/*.c; do \
		$(CC) $(CFLAGS) -c $$f -o $${f%.c}.o; \
	done
	@$(LD) $(LDFLAGS) -o $(BIN_DIR)/$@ $@_crt0.o $@/*.o $(LIB_OBJS)
	@rm -f $@/*.o $@_crt0.o

clean:
	@rm -rf $(BIN_DIR)/*
	@rm -f $(LIB_OBJS) *.o */*.o lib/*/*.o
