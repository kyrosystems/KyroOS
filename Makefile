TOOLCHAIN_PREFIX ?= x86_64-linux-gnu-
CC = $(TOOLCHAIN_PREFIX)gcc
LD = ld
AS = nasm

# skip inc on run/clean
ifeq ($(filter run clean,$(MAKECMDGOALS)),)
_BUILD_INC := $(shell \
	current_build=$$(grep "#define KYROOS_VERSION_BUILD" src/include/version.h | sed 's/[^0-9]//g'); \
	new_build=$$((current_build + 1)); \
	sed "s/\#define KYROOS_VERSION_BUILD \".*\"/\#define KYROOS_VERSION_BUILD \"$$new_build\"/" src/include/version.h > src/include/version.h.tmp && \
	mv src/include/version.h.tmp src/include/version.h \
)
endif

LDFLAGS = -T linker.ld -nostdlib -z noexecstack -z separate-code
OS_VERSION = 26.11.13-Carbon
# cflags
K_CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -O2 -Isrc/include \
           -mcmodel=kernel -mno-red-zone -m64 -nostdlib -fno-stack-protector \
           -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-builtin \
           -mno-sse -mno-sse2 -mno-mmx -mno-80387 -fno-pic -fno-pie \
           -DOS_VERSION="\"$(OS_VERSION)\""
BUILD_DIR = build
OUTPUT_DIR = isofiles
VERSION_TAG = $(shell grep "#define KYROOS_VERSION_MAJOR" src/include/version.h | sed 's/[^0-9]//g').$(shell grep "#define KYROOS_VERSION_MINOR" src/include/version.h | sed 's/[^0-9]//g').$(shell grep "#define KYROOS_VERSION_PATCH" src/include/version.h | sed 's/[^0-9]//g')
BUILD_NUM = $(shell grep "#define KYROOS_VERSION_BUILD" src/include/version.h | sed 's/[^0-9]//g')
ISO_FILENAME = KyroOS-$(VERSION_TAG)-$(BUILD_NUM).iso

# boot objs
K_BOOT_OBJS = $(BUILD_DIR)/boot/boot.o $(BUILD_DIR)/boot/gdt_flush.o $(BUILD_DIR)/boot/isr_stubs.o \
              $(BUILD_DIR)/boot/long_mode_entry.o $(BUILD_DIR)/boot/switch.o $(BUILD_DIR)/boot/userspace_exit_stub.o

K_SRCS = $(wildcard src/kernel/*.c)
K_OBJS = $(patsubst src/kernel/%.c, $(BUILD_DIR)/kernel/%.o, $(K_SRCS))

# qemu flags
QEMU_FLAGS = -cdrom $(ISO_FILENAME) -serial stdio -no-reboot \
             -device e1000,netdev=net0 \
             -netdev user,id=net0,hostname=kyroos,dns=8.8.8.8 \
             -display sdl

.PHONY: all clean run iso userspace modules

all: iso

userspace:
	+@$(MAKE) -C userspace all

modules:
	+@$(MAKE) -C modules/hello_lkm

run: 
	@qemu-system-x86_64 $(QEMU_FLAGS)

iso: userspace modules $(BUILD_DIR)/kernel/kyroos.elf
	@mkdir -p $(BUILD_DIR)/isodir/boot/limine $(BUILD_DIR)/isodir/EFI/BOOT $(BUILD_DIR)/isodir/etc $(BUILD_DIR)/isodir/lib/modules
	@cp $(BUILD_DIR)/kernel/kyroos.elf $(BUILD_DIR)/isodir/boot/kernel.elf
	@cp limine.conf $(BUILD_DIR)/isodir/boot/limine/
	@cp etc/* $(BUILD_DIR)/isodir/etc/
	@if [ -f modules/hello_lkm/hello.ko ]; then cp modules/hello_lkm/hello.ko $(BUILD_DIR)/isodir/lib/modules/; fi
	@for f in $(BUILD_DIR)/isodir/bin/*; do \
		echo "    module_path: boot():/bin/$$(basename $$f)" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf; \
	done
	@for f in $(BUILD_DIR)/isodir/etc/*; do \
		echo "    module_path: boot():/etc/$$(basename $$f)" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf; \
	done
	@if [ -f $(BUILD_DIR)/isodir/lib/modules/hello.ko ]; then \
		echo "    module_path: boot():/lib/modules/hello.ko" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf; \
	fi
	@echo "    module_path: boot():/boot/kernel.elf" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf
	@echo "    module_path: boot():/boot/limine/limine.conf" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf
	@echo "    module_path: boot():/boot/limine/BOOTX64.EFI" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf
	@echo "    module_path: boot():/boot/limine/limine-bios.sys" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf
	@echo "    module_path: boot():/boot/limine/limine-bios-cd.bin" >> $(BUILD_DIR)/isodir/boot/limine/limine.conf
	@cp limine/BOOTX64.EFI limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin $(BUILD_DIR)/isodir/boot/limine/
	@cp limine/BOOTX64.EFI $(BUILD_DIR)/isodir/EFI/BOOT/BOOTX64.EFI
	@rm -rf /tmp/kyroos_build
	@cp -r $(BUILD_DIR)/isodir /tmp/kyroos_build
	@xorriso -as mkisofs -R -J -iso-level 3 -volid "KYROOS" \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		/tmp/kyroos_build -o $(ISO_FILENAME)
	@limine/limine bios-install $(ISO_FILENAME) 2>/dev/null || true
	@rm -rf /tmp/kyroos_build
	@echo "ISO created: $(ISO_FILENAME)"

$(BUILD_DIR)/kernel/kyroos.elf: $(K_BOOT_OBJS) $(K_OBJS)
	@$(LD) -T linker.ld -o $@ $^ -m elf_x86_64

$(BUILD_DIR)/kernel/%.o: src/kernel/%.c $(wildcard src/include/*.h)
	@mkdir -p $(@D)
	@$(CC) $(K_CFLAGS) -c $< -o $@

$(BUILD_DIR)/boot/%.o: src/boot/%.asm
	@mkdir -p $(@D)
	@$(AS) -f elf64 $< -o $@

clean:
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
	@make -C userspace clean