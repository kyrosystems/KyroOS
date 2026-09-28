# 16. Build and Development

KyroOS uses standard GNU tools for building, oriented towards cross-compilation for the x86-64 architecture.

## 16.1. Build Tools

The following set of tools is required to build KyroOS:
-   **GCC Cross-Compiler (GNU Compiler Collection):** `x86_64-linux-gnu-gcc` — the primary compiler for C code of the kernel and user applications.
-   **NASM (Netwide Assembler):** An assembler for processing low-level assembly files (`.asm`).
-   **GNU Binutils:** Includes an archiver (`x86_64-linux-gnu-ar`) for creating static libraries and a linker (`x86_64-linux-gnu-ld`) for linking ELF files.
-   **xorriso:** A tool for creating bootable ISO images.
-   **GNU Make:** A build automation system, controlled by the `Makefile`.
-   **Build Environment:** Optimized for operation in Linux or Windows Subsystem for Linux (WSL).

## 16.2. Source Structure

The project has the following key directory structure:

-   `src/`: Root directory for all KyroOS source code.
    -   `src/boot/`: Low-level assembly files (boot, context switching, interrupt stubs).
    -   `src/include/`: Common header files for kernel and user space (APIs, structure definitions).
    -   `src/kernel/`: C source code for kernel components (memory management, scheduler, drivers, system calls, network stack).
    -   `src/tools/`: Source code for user-space system utilities (coreutils, kpm, installer).
-   `userspace/`: User-space source code.
    -   `userspace/lib/`: Static user-mode libraries (simplified libc, `kyroos_gfx` graphics library, `tui` text UI).
    -   `userspace/game/`: Example user application.
    -   `userspace/init/`: The `init` program, launched at OS startup.
-   `modules/`: Source code for Loadable Kernel Modules (LKM).
    -   `modules/hello_lkm/`: Example of a simple LKM.
-   `limine/`: Limine bootloader source code (often used as a Git submodule).
-   `build/`: Output directory for all generated files: object files (`.o`), static libraries (`.a`), executable files (`.elf`).
-   `doc/` and `docs/`: Project documentation.

## 16.3. Build Process

The main `all` target in the `Makefile` performs a complete system build:
1.  All kernel C code is compiled using `x86_64-linux-gnu-gcc` and assembly code with `nasm`.
2.  A static kernel library (`libkyroos_kernel.a`) is created from object files.
3.  The kernel is linked (`x86_64-linux-gnu-ld`) into an executable file `kyroos.elf`, using `linker.ld`.
4.  User-space libraries and applications are compiled.
5.  A bootable ISO image (`kyroos.iso`) is created using `xorriso`, containing the kernel, Limine bootloader, and all user-space executables.

### Compilation Flags

-   **Kernel Flags (`K_CFLAGS`):**
    -   `-ffreestanding`, `-nostdlib`: Indicate the absence of the standard C library.
    -   `-fno-stack-protector`, `-fno-stack-check`: Disable stack security features.
    -   `-mcmodel=kernel`: Special memory model for the kernel.
    -   `-mno-red-zone`: Disables the "red zone" of the stack, important for interrupt safety.
    -   `-m64`, `-march=x86-64`, `-mabi=sysv`: Target architecture x86-64, 64-bit mode, System V ABI.
    -   `-O2`: Optimization level.
    -   `-Wall`, `-Wextra`: Enable all compiler warnings.
-   **User-space Flags (`U_CFLAGS`):** Similar to kernel flags, but may include other options, such as `-std=gnu11` for GNU C extensions.

## 16.4. Debug / Release Configurations

Currently, the system **lacks explicit targets or variables in the `Makefile` for switching between `Debug` and `Release` build configurations**.
-   **Optimization:** Compilation flags (`-O2`) by default indicate an optimized build, characteristic of release versions.
-   **Debug Symbols:** Debug symbols (`-g`) are not included by default, which is also typical for release builds.
-   **`epstein` Flag:** In the kernel code, there is a software flag `epstein` (see `src/kernel/epstein.h`) that allows activating certain debugging functions (e.g., manual panic initiation) at the code level. Its state must be managed manually (e.g., via `#define` in the header file).
To perform debugging in QEMU (`make run`), GDB must be manually attached.
