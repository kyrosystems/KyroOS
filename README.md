# KyroOS - A Modern x86-64 Operating System

KyroOS is a 64-bit operating system for the x86-64 architecture, developed as a learning project. It features a monolithic kernel designed for simplicity, readability, and extensibility.

**Current version:** `26.03.12-Beryllium` (the build number is incremented automatically on every build).

## Key Features

*   **x86-64 Architecture:** Leverages modern 64-bit computing capabilities (higher-half kernel, `-mcmodel=kernel`).
*   **Limine Bootloader:** Boots on both BIOS and UEFI systems from a single hybrid ISO.
*   **Monolithic Kernel:** Simple, readable, and educational kernel design.
*   **Memory Management:** Physical Memory Manager (PMM) and Virtual Memory Manager (VMM) with demand paging.
*   **Multitasking:** Preemptive, round-robin scheduler for efficient task switching.
*   **Basic Drivers:** Support for keyboard, mouse, framebuffer, IDE hard disks, E1000 network cards, and AC97 audio.
*   **Virtual File System (VFS):** Unified interface for file operations, with KyroFS (in-memory and disk-based) as the primary filesystem.
*   **Userspace Environment:** A basic shell (`kyroshell`) and a set of core utilities (`cat`, `ls`, `mkdir`, etc.), loaded at boot as Limine modules from `/bin`.
*   **Networking Stack:** Support for ARP, IP, UDP, and a stubbed TCP implementation.
*   **Package Manager (`kpm`):** Simple command-line tool for managing userspace software (not operational yet).
*   **Installer:** An automated installer to prepare disk partitions (not operational yet).

## System Requirements

*   **CPU:** x86-64 compatible processor.
*   **RAM:** Minimum 24 MB.
*   **Storage:** Minimum 100 MB flash drive (The installation of the system will be fully implemented in the future)
*   **Boot:** BIOS or UEFI firmware.

## Quick Build Overview

KyroOS is built using a standard GNU toolchain (GCC, Make, NASM, xorriso, LD). It can be built on a native Linux environment or on Windows Subsystem for Linux (WSL).

1.  **Install Toolchain:** On a Debian-based system (like Ubuntu or WSL):
    ```bash
    sudo apt-get update
    sudo apt-get install build-essential nasm xorriso gcc-x86-64-linux-gnu qemu-system-x86
    ```
2.  **Build Project:** Navigate to the project root and run:
    ```bash
    make all
    ```
    This builds the userspace programs (`make -C userspace all`), compiles the kernel (`build/kernel/kyroos.elf`), and creates a bootable ISO named `KyroOS-<major>.<minor>.<patch>-<build>.iso` (the version is read from `src/include/version.h`).
3.  **Run in QEMU:**
    ```bash
    make run
    ```
    QEMU starts with the ISO, serial output redirected to stdio, an E1000 NIC (user-mode networking) and an SDL display.
4.  **Clean:**
    ```bash
    make clean
    ```

### Build Options

*   **`TOOLCHAIN_PREFIX`:** Cross-compiler prefix, `x86_64-linux-gnu-` by default. Override it if your GCC is named differently, e.g. `make all TOOLCHAIN_PREFIX=x86_64-elf-`.
*   **`OS_VERSION`:** Version string passed to the kernel as a `-DOS_VERSION` define (set in the `Makefile`).
*   **Build number:** Every `make` invocation except `run` and `clean` increments `KYROOS_VERSION_BUILD` in `src/include/version.h`.
*   **Compiler include path:** The `Makefile` currently adds a distribution-specific GCC include directory via `-isystem` (openSUSE layout). If the build fails on missing freestanding headers, adjust this path for your distribution.

## Repository Layout

*   `src/` - Kernel sources (`src/boot` for NASM assembly, `src/kernel` for C, `src/include` for headers).
*   `userspace/` - Userspace programs and shell.
*   `modules/` - Loadable kernel modules.
*   `abi/` - Kernel/userspace ABI definitions.
*   `limine/`, `limine.conf` - Bootloader binaries and configuration.
*   `linker.ld` - Kernel linker script.
*   `tools/`, `tests/` - Helper tools and tests.
*   `docs_en/`, `docs_ru/` - Documentation.

## Documentation

The full documentation for KyroOS can be found in the `docs_en/` or `docs_ru/` directory.

### English Documentation

*   [Overview](docs_en/overview.md)
*   [Build](docs_en/build.md) / [Build and Run (Dev)](docs_en/build_and_run_dev.md) / [Debugging](docs_en/debug.md)
*   [Boot Process](docs_en/boot.md)
*   [Kernel](docs_en/kernel.md) / [Memory](docs_en/memory.md) / [Interrupts](docs_en/interrupts.md) / [IPC](docs_en/ipc.md)
*   [System Calls](docs_en/syscalls.md) / [Security](docs_en/security.md) / [Kernel Panics](docs_en/kernel_panics.md)
*   [Drivers](docs_en/drivers.md) / [Filesystem](docs_en/filesystem.md) / [Networking](docs_en/networking.md) / [TLS/HTTPS Status](docs_en/tls_https_status.md)
*   [Graphics](docs_en/graphics.md) / [Graphics Guide](docs_en/graphics_guide.md) / [Graphics Applications](docs_en/graphics_applications.md)
*   [Loadable Kernel Modules](docs_en/lkm.md)
*   [Userspace](docs_en/userspace.md) / [POSIX Commands](docs_en/posix_commands.md)
*   [Porting Applications to KyroOS](docs_en/porting_applications.md)
*   [Writing Applications for KyroOS](docs_en/writing_applications.md)
*   [Limitations](docs_en/limitations.md) / [Future Plans](docs_en/future_plans.md)

### Русская Документация

*   [Обзор](docs_ru/overview.md)
*   [Сборка](docs_ru/build.md) / [Сборка и запуск (Dev)](docs_ru/build_and_run_dev.md) / [Отладка](docs_ru/debug.md)
*   [Процесс загрузки](docs_ru/boot.md)
*   [Ядро](docs_ru/kernel.md) / [Память](docs_ru/memory.md) / [Прерывания](docs_ru/interrupts.md) / [IPC](docs_ru/ipc.md)
*   [Системные вызовы](docs_ru/syscalls.md) / [Безопасность](docs_ru/security.md) / [Kernel Panic](docs_ru/kernel_panics.md)
*   [Драйверы](docs_ru/drivers.md) / [Файловая система](docs_ru/filesystem.md) / [Сеть](docs_ru/networking.md) / [Статус TLS/HTTPS](docs_ru/tls_https_status.md)
*   [Графика](docs_ru/graphics.md) / [Графические приложения](docs_ru/graphics_applications.md)
*   [Пользовательское пространство](docs_ru/userspace.md)
*   [Портирование Приложений на KyroOS](docs_ru/porting_applications.md)
*   [Написание Приложений для KyroOS](docs_ru/writing_applications.md)
*   [Ограничения](docs_ru/limitations.md) / [Планы](docs_ru/future_plans.md)

## Releases

### First Alpha Release (26.01.00 812)

This was the first official alpha release of KyroOS. While the system is still in a very early stage of development, ISO images are available for enthusiasts who want to try it out.

**What to Expect:**

*   The system boots to a basic console
*   You can interact with the basic shell and run a few commands.
*   The keyboard, and framebuffer drivers are functional.

**Known Issues & Limitations:**

*   **Installer is not functional:** You must run the OS from the ISO using a virtual machine (like QEMU or VirtualBox) or by burning it to a USB drive. Installation to a hard disk is not yet supported.
*   **Package Manager (KPM) is not working:** The `kpm` utility is included but is currently non-operational.
*   **High Instability:** The system is extremely raw and may crash or behave unexpectedly. Save your work in other OSes, not this one!

You can find ISO files on the [GitHub Releases](https://github.com/kyrosystems/KyroOS/releases) page. We welcome feedback and bug reports!

## Warning

KyroOS is under heavy development and is considered a **pre-alpha** quality operating system. It is not stable, may contain significant bugs, and is not suitable for production use.
