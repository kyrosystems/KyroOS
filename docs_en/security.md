# 14. Security

The KyroOS security model, being an experimental operating system, focuses on fundamental protection mechanisms provided by hardware to ensure process isolation and protect the kernel from unauthorized access.

## 14.1. Security Model

The basic KyroOS security model is built on the concept of **resource isolation** and **privileges**. Key principles:

-   **Privilege Separation:** Strict separation between privileged kernel mode (Ring 0) and unprivileged user mode (Ring 3).
-   **Memory Isolation:** Each process executes in its own virtual address space, preventing direct access to the memory of other processes or the kernel.
-   **Controlled Resource Access:** All operations with system resources (files, devices, network) are available only through a strictly defined system call API, which is validated by the kernel.

The absence of a full-fledged system for user and group management, as well as detailed file access permissions (chmod, chown), is a significant limitation at this stage.

## 14.2. Process Isolation

Process isolation is the cornerstone of stability and security for a multitasking operating system. KyroOS implements it primarily through virtual memory mechanisms.

-   **Virtual Address Spaces:** Each process has its own 4-level virtual address space, described by a unique PML4 (Page Map Level 4) table. This means that a virtual address `0x100000` in one process might point to a completely different physical memory location than the same address in another process.
-   **No Direct Access:** A user process (running in Ring 3) cannot directly access the memory of other processes or the kernel's memory. Any such attempt generates an exception (e.g., Page Fault or General Protection Fault), which is intercepted by the kernel.
-   **Page Protection Flags:** The Virtual Memory Manager (VMM) sets appropriate protection flags in page table entries:
    -   `PAGE_USER`: Allows access from user mode. If this flag is not set, access is only possible from kernel mode.
    -   `PAGE_WRITE`: Allows writing. Program code pages are typically marked as "read-only".
    -   `PAGE_NO_EXEC` (NX Bit): Prevents code execution from pages marked with this flag (e.g., from the stack or heap), preventing certain types of attacks related to code injection.

## 14.3. Kernel Protection

Protecting the kernel from malicious or erroneous actions by user applications (and, to a lesser extent, from errors within the kernel itself) is a priority.

-   **Kernel Space:** Kernel code and data always reside in the upper half of the virtual address space, isolated from user memory.
-   **Ring 0 Privileges:** Kernel code executes at the maximum privilege level (Ring 0), having full access to all system resources and processor instructions. User processes (Ring 3) cannot execute privileged instructions.
-   **Kernel Memory Protection:** Pages containing kernel code and data are mapped with the `PAGE_SUPERVISOR` flag (which is equivalent to the absence of the `PAGE_USER` flag), making them inaccessible from user mode.
-   **Controlled Transition:** The only authorized path for transitioning from user mode to kernel mode is the **system call mechanism** (`int 0x80`). All parameters passed by a user application to a system call must be thoroughly validated by the kernel for validity and security before being used.
-   **Critical Error Handling:** Any unhandled exception (e.g., Page Fault or General Protection Fault) in kernel mode immediately leads to a **Kernel Panic**. This prevents potential data corruption or further system instability, signaling a fatal error.
-   **No LKM for User Applications:** Despite supporting Loadable Kernel Modules, the module loading mechanism is controlled only by the kernel and is not provided to user applications, which prevents the injection of unauthorized code into the kernel.
