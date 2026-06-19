TOOLCHAIN_PREFIX ?= x86_64-linux-gnu-
CC = $(TOOLCHAIN_PREFIX)gcc
LD = ld
AS = nasm

# Increment build number on every make invocation
_BUILD_INC := $(shell \
	current_build=$$(grep "#define KYROOS_VERSION_BUILD" src/include/version.h | sed 's/[^0-9]//g'); \
	new_build=$$((current_build + 1)); \
	sed "s/\#define KYROOS_VERSION_BUILD \".*\"/\#define KYROOS_VERSION_BUILD \"$$new_build\"/" src/include/version.h > src/include/version.h.tmp && \
	mv src/include/version.h.tmp src/include/version.h \
)

OS_VERSION = 26.03.12-Beryllium
K_CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -O2 -Isrc/include \
           -mcmodel=kernel -mno-red-zone -m64 -nostdlib -fno-stack-protector \
           -isystem /usr/lib64/gcc/x86_64-suse-linux/15/include \
           -mno-sse -mno-sse2 -mno-mmx -mno-80387 -fno-pic -fno-pie \
           -DOS_VERSION="\"$(OS_VERSION)\""
BUILD_DIR = build
OUTPUT_DIR = isofiles
ISO_FILENAME = /tmp/KyroOS-Beryllium.iso

K_BOOT_OBJS = $(BUILD_DIR)/boot/boot.o $(BUILD_DIR)/boot/gdt_flush.o $(BUILD_DIR)/boot/isr_stubs.o \
              $(BUILD_DIR)/boot/long_mode_entry.o $(BUILD_DIR)/boot/switch.o $(BUILD_DIR)/boot/userspace_exit_stub.o

K_SRCS = $(wildcard src/kernel/*.c)
K_OBJS = $(patsubst src/kernel/%.c, $(BUILD_DIR)/kernel/%.o, $(K_SRCS))


# QEMU Flags
QEMU_FLAGS = -cdrom $(ISO_FILENAME) -serial stdio -no-reboot \
             -device e1000,netdev=net0 \
             -netdev user,id=net0,hostname=kyroos,dnssearch=8.8.8.8 \
             -display gtk -vga std

.PHONY: all clean run iso userspace

all: userspace $(BUILD_DIR)/kernel/kyroos.elf iso

userspace:
	@make -C userspace all

run: all
	@qemu-system-x86_64 $(QEMU_FLAGS)

iso:
	@mkdir -p $(BUILD_DIR)/isodir/boot/limine
	@cp $(BUILD_DIR)/kernel/kyroos.elf $(BUILD_DIR)/isodir/boot/kernel.elf
	@cp limine.conf $(BUILD_DIR)/isodir/boot/limine/
	@cp limine/BOOTX64.EFI limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin $(BUILD_DIR)/isodir/boot/limine/
	@rm -rf /tmp/kyroos_build
	@cp -r $(BUILD_DIR)/isodir /tmp/kyroos_build
	@xorriso -as mkisofs -R -J -iso-level 3 -volid "KYROOS" \
	   -b boot/limine/limine-bios-cd.bin \
	   -no-emul-boot -boot-load-size 4 -boot-info-table \
	   --efi-boot boot/limine/BOOTX64.EFI \
	   -efi-boot-part --efi-boot-image --protective-msdos-label \
	   /tmp/kyroos_build -o $(ISO_FILENAME)
	@limine/limine bios-install $(ISO_FILENAME) 2>/dev/null || true
	@rm -rf /tmp/kyroos_build
	@echo "ISO created: $(ISO_FILENAME)"

$(BUILD_DIR)/kernel/kyroos.elf: $(K_BOOT_OBJS) $(K_OBJS)
	@$(LD) -T linker.ld -o $@ $^ -m elf_x86_64

$(BUILD_DIR)/kernel/%.o: src/kernel/%.c src/include/version.h
	@mkdir -p $(@D)
	@$(CC) $(K_CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot/%.o: src/boot/%.asm
	@mkdir -p $(@D)
	@$(AS) -f elf64 $< -o $@

clean:
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
	@make -C userspace clean
