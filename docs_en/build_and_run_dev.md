# Building and running KyroOS (dev)

This guide describes the current root Makefile. Building an ISO does not prove that the system boots or that userspace ELF files execute correctly. Run these commands from the repository root on Linux.

## Tools

You need `make`, an x86_64 GCC, GNU `ld`, `nasm`, `xorriso`, and `qemu-system-x86_64`. The userspace build runs as a recursive make; see `userspace/Makefile` for its configuration.

```sh
command -v make gcc ld nasm xorriso qemu-system-x86_64
```

The root Makefile hardcodes `-isystem /usr/lib64/gcc/x86_64-suse-linux/15/include`; this path may need adjusting for your GCC and distribution. Check `TOOLCHAIN_PREFIX` near the start of the Makefile before building.

The `limine/` directory contains `BOOTX64.EFI`, `limine-bios.sys`, `limine-bios-cd.bin`, and `limine-uefi-cd.bin`, but not the `limine/limine` installation tool. The ISO recipe suppresses `bios-install` failures with `|| true`. An ISO being created therefore does not prove BIOS boot works.

## Build

```sh
git clone --branch dev https://github.com/kyrosystems/KyroOS.git
cd KyroOS
make all
```

The `all` target builds userspace, then the kernel and ISO. The kernel output is `build/kernel/kyroos.elf`; the ISO is placed at the repository root as `KyroOS-<version>-<build>.iso`. A regular build changes `KYROOS_VERSION_BUILD` in `src/include/version.h`; check `git status` before committing.

`make iso` expects a built kernel. `make run` does not rebuild anything: after changing code, run `make all` before `make run`.

## Run and diagnose faults

```sh
make run
```

The Makefile runs QEMU with a CD-ROM ISO, serial output, `-no-reboot`, an e1000 network adapter, and an SDL display. To capture exceptions, substitute the actual ISO name:

```sh
ls -lt KyroOS-*.iso
qemu-system-x86_64 -cdrom ./KyroOS-<version>-<build>.iso -serial stdio -no-reboot -d int,cpu_reset -D qemu.log -display none
```

The angle-bracket parts above are placeholders, not shell syntax. `-display none` hides the framebuffer; use `make run` to inspect graphics. For a triple fault, report the first preceding exception in `qemu.log` and the serial output.

## Image contents and cleanup

The ISO recipe copies the kernel to `/boot/kernel.elf`, adds `module_path` entries for files under `build/isodir/bin/` to `limine.conf`, and copies Limine boot files. `kyrofs_init` imports boot modules into the filesystem, and the shell tries `/bin/<name>` through the ELF loader. Having a binary in the ISO is not proof it runs in ring 3.

```sh
make clean
```

The clean target removes build directories and runs `make -C userspace clean`. Check the date and version of any old ISO left in the repository root before testing it.
