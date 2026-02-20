# 12. Userspace

The userspace in KyroOS is a low-privilege environment (Ring 3) where all application programs execute. It is completely isolated from the kernel and other processes. Interaction with the kernel and system resources is exclusively done through the system call API.

## 12.1. Executable File Format

KyroOS uses the standard, widely adopted executable file format **ELF64 (Executable and Linkable Format, 64-bit)**.

-   **Specification:** The kernel contains a loader capable of parsing ELF64 headers (`Elf64_Ehdr`) and program headers (`Elf64_Phdr`).
-   **Type:** The loader only supports executable files (`ET_EXEC`). Relocatable (`ET_REL`) and shared (`ET_DYN`) object files are not supported.
-   **Build:** User applications must be statically linked to execute at fixed virtual addresses in the lower half of the address space.

## 12.2. Program Loading

The process of loading and launching a program is initiated by a system call `exec` (or similar) and consists of the following steps:

1.  **Address Space Creation:** The kernel creates a new, empty 4-level page table mapping (PML4) for the new process. The upper half of this space (kernel addresses) is copied from the kernel's template, while the lower half (user space) remains empty.
2.  **File Reading:** The kernel reads the entire ELF file into a buffer in its memory.
3.  **ELF Analysis:** The `elf_load` loader verifies the correctness of the ELF header (magic number, class, type).
4.  **Segment Loading:** The loader iterates through all program headers (`Program Headers`).
    -   For each segment of type `PT_LOAD` (loadable segment):
    -   The kernel allocates the necessary physical memory pages.
    -   These pages are mapped into the new process's virtual address space at the address (`p_vaddr`) specified in the segment header.
    -   Page protection flags (`Read`, `Write`, `Execute`) are set according to the segment's flags. User access (`PAGE_USER`) is always permitted.
    -   Segment data is copied from the buffer into the newly allocated pages. If the memory size (`p_memsz`) is larger than the file size (`p_filesz`), the remainder (the `.bss` section) is zero-filled.
5.  **Thread Creation:** After successfully loading all segments, the kernel creates a new thread in the `READY` state (`thread_create_userspace`). The entry point for this thread is set to the address (`e_entry`) specified in the ELF header, and the address space is the newly created one.
6.  **Execution:** On the next scheduler invocation, the new thread will be selected for execution and will begin its work in user mode (Ring 3) from the program's entry point.

## 12.3. Application API

The primary and sole API for applications to interact with the kernel is the **system call interface**. Details of the mechanism, ABI, and a complete list of calls are described in **[7. System Calls](./syscalls.md)**.

## 12.4. Standard Libraries

A full-fledged C standard library (`libc`) is absent in KyroOS. Instead, a set of small, specialized libraries and self-contained system call wrappers are provided.

### `syscall()` Wrapper

Since there is no central `libc`, each application or library that needs to make system calls uses its own local implementation of the `syscall()` function. This is a small function written in embedded assembly (GCC inline assembly) that performs the following actions:
1.  Places the system call number into the `rax` register.
2.  Places arguments into `rdi`, `rsi`, `rdx`, etc., registers.
3.  Executes the `int 0x80` instruction.
4.  Returns the value from the `rax` register after the call completes.

### `libkyroos_gfx`

A simple graphics library that provides basic drawing functions.
-   **Initialization:** During initialization (`gfx_init()`), the library calls `SYS_GFX_GET_FB_INFO` to obtain direct access to video memory from the kernel.
-   **Functionality:** Provides functions `gfx_draw_pixel`, `gfx_draw_rect`, which directly write data to the framebuffer memory. It also includes a wrapper for `SYS_INPUT_POLL_EVENT` to receive input events.

### `libtui`

A library for creating Text User Interfaces. It is a higher-level abstraction built on top of `libkyroos_gfx`.
-   **Functionality:** Uses the framebuffer obtained from the kernel to render text using an embedded bitmap font (`tui_draw_text`), as well as to draw simple graphic primitives like rectangles (`tui_draw_box`).
-   **Application:** Used to create pseudo-graphical window interfaces without a full-fledged graphical subsystem.
