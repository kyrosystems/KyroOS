# KyroOS - A Modern x86-64 Operating System

KyroOS is an 64-bit operating system for the x86-64 architecture, developed as a learning project. It features a monolithic kernel designed for simplicity, readability, and extensibility.

## Key Features

*   **x86-64 Architecture:** Leverages modern 64-bit computing capabilities.
*   **Monolithic Kernel:** Simple, readable, and educational kernel design.
*   **Memory Management:** Physical Memory Manager (PMM) and Virtual Memory Manager (VMM) with demand paging.
*   **Multitasking:** Preemptive, round-robin scheduler for efficient task switching.
*   **Basic Drivers:** Support for keyboard, mouse, framebuffer, IDE hard disks, E1000 network cards, and AC97 audio.
*   **Virtual File System (VFS):** Unified interface for file operations, with KyroFS (in-memory and disk-based) as the primary filesystem.
*   **Userspace Environment:** A basic shell (`kyroshell`) and a set of core utilities (`cat`, `ls`, `mkdir`, etc.).
*   **Networking Stack:** Support for ARP, IP, UDP, and a stubbed TCP implementation.
*   **Package Manager (`kpm`):** Simple command-line tool for managing userspace software.
*   **Installer:** An automated installer to prepare disk partitions.

## System Requirements

*   **CPU:** x86-64 compatible processor.
*   **RAM:** Minimum 24 MB.
*   **Storage:** Minimum 100 MB flash drive (The installation of the system will be fully implemented in the future)
*   **Boot:** BIOS or UEFI firmware.

## Quick Build Overview

KyroOS is built using a standard GNU toolchain (GCC, Make, NASM, xorriso). It can be built on a native Linux environment or on Windows Subsystem for Linux (WSL).

1.  **Install Toolchain:** On a Debian-based system (like Ubuntu or WSL):
    ```bash
    sudo apt-get update
    sudo apt-get install build-essential nasm xorriso gcc-x86-64-linux-gnu
    ```
2.  **Build Project:** Navigate to the project root and run:
    ```bash
    make all
    ```
    This will compile the kernel and userspace programs, and create `kyroos.iso`.
## Documentation

The full documentation for KyroOS can be found in the `docs_en/` or `docs_ru/` directory.

### English Documentation

*   [Porting Applications to KyroOS](docs_en/porting_applications.md)
*   [Writing Applications for KyroOS](docs_en/writing_applications.md)

### Русская Документация

*   [Портирование Приложений на KyroOS](docs_ru/porting_applications.md)
*   [Написание Приложений для KyroOS](docs_ru/writing_applications.md)

## First Alpha Release (26.01.00 812)

This is the first official alpha release of KyroOS. While the system is still in a very early stage of development, we are making an ISO available for enthusiasts who want to try it out.

**What to Expect:**

*   The system boots to a graphical desktop environment.
*   You can interact with the basic shell and run a few commands.
*   The keyboard, and framebuffer drivers are functional.

**Known Issues & Limitations:**

*   **Installer is not functional:** You must run the OS from the ISO using a virtual machine (like QEMU or VirtualBox) or by burning it to a USB drive. Installation to a hard disk is not yet supported.
*   **Package Manager (KPM) is not working:** The `kpm` utility is included but is currently non-operational.
*   **High Instability:** The system is extremely raw and may crash or behave unexpectedly. Save your work in other OSes, not this one!

You can find the ISO file on the [GitHub Releases](https://github.com/pon4ikisdonut/KyroOS/releases) page. We welcome feedback and bug reports!

## Warning

KyroOS is under heavy development and is considered an **alpha** quality operating system. It is not stable, may contain significant bugs, and is not suitable for production use.
