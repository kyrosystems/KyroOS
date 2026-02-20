TOOLCHAIN_PREFIX ?= x86_64-linux-gnu-
ifeq ($(shell which $(TOOLCHAIN_PREFIX)gcc 2>/dev/null),)
    ifeq ($(shell which x86_64-pc-linux-gnu-gcc 2>/dev/null),)
        TOOLCHAIN_PREFIX =
        $(warning No cross-compiler found. Using host gcc.)
    else
        TOOLCHAIN_PREFIX = x86_64-pc-linux-gnu-
    endif
endif
CC = $(TOOLCHAIN_PREFIX)gcc
AR = $(TOOLCHAIN_PREFIX)ar
LD = $(TOOLCHAIN_PREFIX)ld
AS = nasm
VERSION_H = src/include/version.h
V_MAJOR = $(shell grep -oP 'KYROOS_VERSION_MAJOR \K\d+' $(VERSION_H))
V_MINOR = $(shell grep -oP 'KYROOS_VERSION_MINOR \K\d+' $(VERSION_H))
V_PATCH = $(shell grep -oP 'KYROOS_VERSION_PATCH \K\d+' $(VERSION_H))
V_BUILD = $(shell grep -oP 'KYROOS_VERSION_BUILD \K"\K\d+' $(VERSION_H) | sed 's/"//')
FULL_VERSION = $(V_MAJOR).$(V_MINOR).$(V_PATCH)-$(V_BUILD)
BUILD_DIR = build
ISO_DIR   = $(BUILD_DIR)/isodir
OUTPUT_DIR = isofiles
SRC_DIR   = src
USER_DIR  = userspace
TOOLS_SRC = src/tools
MODULES_DIR = modules
KERNEL = $(BUILD_DIR)/kernel/kyroos.elf
ISO_FILENAME = $(OUTPUT_DIR)/KyroOS-$(FULL_VERSION).iso
K_CFLAGS = -Wall -Wextra -std=c11 -ffreestanding -O2 -I$(SRC_DIR)/include \
           -I$(SRC_DIR)/include/drivers/gpu/amd_gpu \
           -mcmodel=kernel -mno-red-zone -m64 -nostdlib -fno-stack-protector \
           -mno-sse -mno-sse2 -mno-mmx -mno-80387 -fno-pic -fno-pie
NASMFLAGS = -f elf64
U_CFLAGS = -Wall -Wextra -std=c11 -I$(USER_DIR)/lib -I$(USER_DIR)/lib/libc -I$(USER_DIR)/lib/kyroos_gfx \
           -I$(USER_DIR)/lib/tui -I$(SRC_DIR)/include -I$(TOOLS_SRC) -ffreestanding -nostdlib \
           -fno-stack-protector -fno-pie -m64 -O2
K_BOOT_OBJS = $(BUILD_DIR)/boot/boot.o $(BUILD_DIR)/boot/gdt_flush.o $(BUILD_DIR)/boot/isr_stubs.o \
              $(BUILD_DIR)/boot/long_mode_entry.o $(BUILD_DIR)/boot/switch.o $(BUILD_DIR)/boot/userspace_exit_stub.o
K_OBJS_C = $(patsubst $(SRC_DIR)/kernel/%.c, $(BUILD_DIR)/kernel/%.o, $(wildcard $(SRC_DIR)/kernel/*.c))

# New: AMD GPU driver source files
K_DRIVER_GPU_AMDGPU_SRCS = $(wildcard $(SRC_DIR)/kernel/drivers/gpu/amd_gpu/*.c)
K_DRIVER_GPU_AMDGPU_OBJS = $(patsubst $(SRC_DIR)/kernel/drivers/gpu/amd_gpu/%.c, $(BUILD_DIR)/kernel/drivers/gpu/amd_gpu/%.o, $(K_DRIVER_GPU_AMDGPU_SRCS))

# Extend K_OBJS_C to include AMD GPU driver objects
K_OBJS_C += $(K_DRIVER_GPU_AMDGPU_OBJS)
USER_LIBC_A   = $(BUILD_DIR)/userspace/libkyroos_user.a
USER_CRT0_OBJ = $(BUILD_DIR)/userspace/crt0.o
USER_LINKER   = $(USER_DIR)/linker.ld
INIT_ELF      = $(BUILD_DIR)/userspace/init/init
KPM_ELF       = $(BUILD_DIR)/tools/kpm/kpm
INSTALLER_ELF = $(BUILD_DIR)/tools/installer/installer
COREUTILS_SRCS = $(wildcard $(TOOLS_SRC)/coreutils/*.c)
COREUTILS_ELFS = $(patsubst $(TOOLS_SRC)/coreutils/%.c, $(BUILD_DIR)/bin/%, $(COREUTILS_SRCS))
APP_DIRS = $(filter-out $(USER_DIR)/init $(USER_DIR)/lib $(USER_DIR)/micropython $(USER_DIR)/crt0.asm $(USER_DIR)/linker.ld, $(wildcard $(USER_DIR)/*))
_APP_SRCS_FULL_PATHS = $(foreach dir,$(APP_DIRS),$(dir)/main.c)
USER_APP_SRCS = $(filter $(_APP_SRCS_FULL_PATHS), $(wildcard $(_APP_SRCS_FULL_PATHS)))
USER_APP_OBJS = $(patsubst $(USER_DIR)/%/main.c, $(BUILD_DIR)/userspace/%/%.o, $(USER_APP_SRCS))
USER_APP_ELFS = $(patsubst $(BUILD_DIR)/userspace/%/%.o, $(BUILD_DIR)/userspace/%/%, $(USER_APP_OBJS))
.PHONY: all clean run iso update_version userspace tools modules coreutils
all: update_version $(KERNEL) $(USER_LIBC_A) userspace tools modules coreutils iso
run: all
	@echo "--- Запуск KyroOS $(FULL_VERSION) в QEMU ---"
	@qemu-system-x86_64 -cdrom $(ISO_FILENAME) -serial stdio -no-reboot
iso:
	@mkdir -p $(ISO_DIR)/boot/limine $(ISO_DIR)/bin $(ISO_DIR)/modules $(ISO_DIR)/etc $(OUTPUT_DIR)
	@cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	@cp limine.conf $(ISO_DIR)/boot/limine/
	@cp limine/BOOTX64.EFI limine/limine-bios.sys \
	    limine/limine-bios-cd.bin limine/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/
	@cp $(INIT_ELF) $(ISO_DIR)/bin/ 2>/dev/null || true
	@cp $(KPM_ELF) $(ISO_DIR)/bin/ 2>/dev/null || true
	@cp $(INSTALLER_ELF) $(ISO_DIR)/bin/ 2>/dev/null || true
	@if [ -d $(BUILD_DIR)/bin ]; then cp $(BUILD_DIR)/bin/* $(ISO_DIR)/bin/ 2>/dev/null || true; fi
	@$(foreach app,$(USER_APP_ELFS), if [ -f $(app) ]; then cp $(app) $(ISO_DIR)/bin/; fi ;)
	@cp panic_quotes.txt $(ISO_DIR)/etc/ 2>/dev/null || true
	@xorriso -as mkisofs -R -J -iso-level 3 -volid "KYROOS" \
	   -b boot/limine/limine-bios-cd.bin \
	   -no-emul-boot -boot-load-size 4 -boot-info-table \
	   --efi-boot boot/limine/BOOTX64.EFI \
	   -efi-boot-part --efi-boot-image --protective-msdos-label \
	   $(ISO_DIR) -o $(ISO_FILENAME)
	@limine/limine bios-install $(ISO_FILENAME) 2>/dev/null || echo "Limine install skipped."
	@rm -rf $(ISO_DIR)
	@echo "--- ISO готова: $(ISO_FILENAME) ---"
update_version:
	@sed -i -E 's/^#define KYROOS_VERSION_BUILD "[0-9]+"/#define KYROOS_VERSION_BUILD "'$$(($(V_BUILD)+1))'"/' $(VERSION_H)
	$(eval V_BUILD = $(shell grep -oP 'KYROOS_VERSION_BUILD "[0-9]+"' $(VERSION_H) | grep -oP '[0-9]+'))
	$(eval FULL_VERSION = $(V_MAJOR).$(V_MINOR).$(V_PATCH)-$(V_BUILD))
	$(eval ISO_FILENAME = $(OUTPUT_DIR)/KyroOS-$(FULL_VERSION).iso)
$(KERNEL): $(K_BOOT_OBJS) $(K_OBJS_C)
	@mkdir -p $(@D)
	@$(LD) -T linker.ld -o $@ $^ -m elf_x86_64
$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(@D)
	@$(CC) $(K_CFLAGS) -c $< -o $@
$(BUILD_DIR)/kernel/drivers/gpu/amd_gpu/%.o: $(SRC_DIR)/kernel/drivers/gpu/amd_gpu/%.c
	@mkdir -p $(@D)
	@$(CC) $(K_CFLAGS) -c $< -o $@
$(BUILD_DIR)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@mkdir -p $(@D)
	@$(AS) $(NASMFLAGS) $< -o $@
$(BUILD_DIR)/userspace/crt0.o: userspace/crt0.asm
	@mkdir -p $(@D)
	@$(AS) $(NASMFLAGS) $< -o $@
$(BUILD_DIR)/userspace/init/init.o: $(USER_DIR)/init/init.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
$(USER_LIBC_A): $(patsubst $(USER_DIR)/lib/libc/%.c, $(BUILD_DIR)/userspace/libc/%.o, $(wildcard $(USER_DIR)/lib/libc/*.c)) \
                 $(patsubst $(USER_DIR)/lib/kyroos_gfx/%.c, $(BUILD_DIR)/userspace/kyroos_gfx/%.o, $(wildcard $(USER_DIR)/lib/kyroos_gfx/*.c)) \
                 $(patsubst $(USER_DIR)/lib/tui/%.c, $(BUILD_DIR)/userspace/tui/%.o, $(wildcard $(USER_DIR)/lib/tui/*.c))
	@mkdir -p $(@D)
	@$(AR) rcs $@ $^
$(BUILD_DIR)/userspace/libc/%.o: $(USER_DIR)/lib/libc/%.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
$(BUILD_DIR)/userspace/kyroos_gfx/%.o: $(USER_DIR)/lib/kyroos_gfx/%.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
$(BUILD_DIR)/userspace/tui/%.o: $(USER_DIR)/lib/tui/%.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
coreutils: $(COREUTILS_ELFS)
$(BUILD_DIR)/bin/%: $(BUILD_DIR)/tools/coreutils/%.o $(USER_CRT0_OBJ) $(USER_LIBC_A)
	@mkdir -p $(@D)
	@$(LD) -T $(TOOLS_SRC)/coreutils/link.ld -o $@ $^
$(BUILD_DIR)/tools/coreutils/%.o: $(TOOLS_SRC)/coreutils/%.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
userspace: $(INIT_ELF) $(USER_APP_ELFS)
define LINK_APP_TEMPLATE
$(1): $(patsubst %/%,%/%.o,$(1)) $(USER_CRT0_OBJ) $(USER_LIBC_A)
	@mkdir -p $$(dir $$@)
	@$(LD) -T $(USER_LINKER) -o $$@ $$^
endef
$(foreach app_elf,$(USER_APP_ELFS),$(eval $(call LINK_APP_TEMPLATE,$(app_elf))))
$(foreach app_obj,$(USER_APP_OBJS),$(eval $(app_obj): $(patsubst $(BUILD_DIR)/userspace/%/%.o,$(USER_DIR)/%/main.c,$(app_obj)) ; @mkdir -p $$(dir $$@); $(CC) $(U_CFLAGS) -c $$< -o $$@))
$(INIT_ELF): $(BUILD_DIR)/userspace/init/init.o $(USER_CRT0_OBJ) $(USER_LIBC_A)
	@mkdir -p $(@D)
	@$(LD) -T $(USER_LINKER) -o $@ $^
tools: $(KPM_ELF) $(INSTALLER_ELF)
$(KPM_ELF): $(patsubst $(TOOLS_SRC)/kpm/%.c, $(BUILD_DIR)/tools/kpm/%.o, $(wildcard $(TOOLS_SRC)/kpm/*.c)) $(USER_CRT0_OBJ) $(USER_LIBC_A)
	@mkdir -p $(@D)
	@$(LD) -T $(TOOLS_SRC)/kpm/link.ld -o $@ $^
$(BUILD_DIR)/tools/kpm/%.o: $(TOOLS_SRC)/kpm/%.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
$(INSTALLER_ELF): $(BUILD_DIR)/tools/installer/installer.o $(USER_CRT0_OBJ) $(USER_LIBC_A)
	@mkdir -p $(@D)
	@$(LD) -T $(TOOLS_SRC)/installer/link.ld -o $@ $^
$(BUILD_DIR)/tools/installer/installer.o: $(TOOLS_SRC)/installer/installer.c
	@mkdir -p $(@D)
	@$(CC) $(U_CFLAGS) -c $< -o $@
modules:
	$(MAKE) -C $(MODULES_DIR)/hello_lkm || true 
clean:
	@echo "--- Очистка проекта KyroOS ---"
	@# Удаляем директории сборки и вывода
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
	@# Очищаем модули (вызываем их собственные clean, если есть)
	@$(MAKE) -C $(MODULES_DIR)/hello_lkm clean || true
	@# (Опционально) Если нужно сбросить счетчик билдов в version.h, добавь команду сюда
	@echo "Очистка завершена."