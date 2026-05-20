# Porting Applications to KyroOS

Porting applications to KyroOS involves adapting them to its unique environment, which is significantly different from typical Linux/Unix or Windows systems. KyroOS is an experimental kernel with a limited set of system calls and a custom C library.

## Understanding the KyroOS Environment

KyroOS aims for simplicity and efficiency. Key aspects to consider:

*   **System Calls:** KyroOS provides a custom set of system calls. You will need to refer to `src/include/syscall.h` for the available functions and their signatures. Standard POSIX system calls are generally *not* directly available.
*   **C Standard Library:** A subset of the C standard library is available, primarily implemented in `userspace/lib/`. Review these files to understand what functions are provided (e.g., string manipulation, memory allocation). You may need to port missing functions or implement them yourself.
*   **Userspace Entry Point:** Applications typically start with a custom `_start` routine (see `userspace/crt0.asm`) that initializes the environment before calling `main`.
*   **Memory Management:** Userspace applications operate within their own virtual memory space. Memory allocation (e.g., `malloc`, `free`) is handled by the KyroOS userspace library.
*   **Toolchain:** KyroOS applications are compiled with `gcc-x86-64-linux-gnu` (or similar cross-compiler) and linked with a custom linker script (`linker.ld` in the project root) that defines the memory layout for userspace programs.

## Porting Process

1.  **Analyze Dependencies:** Identify all external libraries and system calls your application uses.
    *   **System Calls:** Map existing system calls (e.g., `open`, `read`, `write`, `fork`, `exec`) to their KyroOS equivalents or determine if they need to be reimplemented or stubbed out.
    *   **C Library Functions:** Check if all required C standard library functions are present in `userspace/lib/`. If not, you will need to port them.
    *   **Third-party Libraries:** Most complex third-party libraries (e.g., GUI frameworks, networking stacks) will not be directly portable without significant effort, as they often depend heavily on a POSIX-compliant environment. You will likely need to port them line-by-line or find minimal alternatives.

2.  **Adjust Build System:**
    *   **Makefile:** You will likely need a custom `Makefile` for your application that uses the `x86_64-elf-gcc` (or similar cross-compiler) and links against the KyroOS userspace library.
    *   **Linker Script:** Ensure your application uses the correct linker script or defines its sections appropriately for the KyroOS userspace memory layout.
    *   **Include Paths:** Ensure your compiler can find the KyroOS userspace headers (`src/include`).

3.  **Code Modifications:**
    *   **System Call Wrappers:** Write wrappers for any system calls that need adaptation or implement custom system call interfaces.
    *   **Platform-Specific Code:** Replace any platform-specific code (e.g., Windows API calls, Linux-specific `ioctl`s) with KyroOS equivalents or custom implementations.
    *   **Concurrency:** If your application uses threads or processes, adapt them to KyroOS's threading model (refer to `src/include/thread.h` and `src/kernel/thread.c`).

4.  **Testing and Debugging:**
    *   **QEMU/VirtualBox:** Run your ported application within KyroOS in a virtual machine.
    *   **Kernel Logging:** Utilize the KyroOS kernel's logging capabilities (`log.h`) for debugging issues within your application if necessary.

## Example

For examples of userspace applications, examine the `userspace/` directory in the [KyroOS repository](https://github.com/kyrosystems/KyroOS). Applications like `init` and `shell` provide good starting points for understanding the build process and interaction with the kernel.
