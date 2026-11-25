# 18. Limitations and Known Issues

KyroOS is currently under active development (alpha version) and is not intended for use in production environments. The current implementation has a number of limitations and incomplete functionalities.

## 18.1. Architectural Limitations

### Memory Management

-   **Primitive `fs_disk` Block Allocation:** The `fs_disk` file system uses a very simplified block allocation mechanism (`next_free_block` counter), which prevents reuse of freed blocks and leads to rapid disk space fragmentation.
-   **Inefficient `KyroFS` Writing:** Write operations to the in-memory KyroFS file system can be inefficient, especially when files grow, due to frequent `kmalloc`/`kfree` and `memcpy` operations for buffer reallocation.

### Multitasking and Synchronization

-   **Lack of Full Synchronization Primitives:** The kernel lacks mutexes, semaphores, and spinlocks. The only synchronization mechanism is global interrupt disabling/enabling, which is a coarse-grained method and unsuitable for long-term blocking.
-   **Lack of Multi-core Support (SMP):** The kernel is designed for single-processor systems. The absence of spinlocks and other shared data protection mechanisms makes it unsuitable for multi-core processors without significant modifications.
-   **Incomplete Thread Blocking Mechanism:** Despite the existence of the `THREAD_BLOCKED` state, a full-fledged mechanism for putting threads to sleep and waking them up (e.g., based on an event or resource availability) is only partially implemented and requires further development.

### File System

-   **Low Scalability of `fs_disk`:** `fs_disk` is a flat file system (no support for hierarchical directories) with a strict limit on the number of files (32). It is not integrated into the VFS.
-   **Lack of Access Control Model:** Neither the VFS nor concrete FS implementations (`KyroFS`, `fs_disk`) support a full-fledged access control model (user, group, others; read, write, execute).
-   **Lack of Data Caching in `fs_disk`:** The disk FS does not cache data blocks, leading to direct disk access for every read/write operation and reduced performance.

### Networking

-   **Tight Coupling of Network Core and Driver:** Core network functions (`net_init`, `net_register_device`) are implemented within the E1000 driver, making the subsystem less modular and hindering the addition of other network drivers.
-   **Partial TCP Implementation:** The TCP protocol is not fully implemented, although it includes a state machine. Issues with reliability, performance, and specification compliance may exist.

### Graphics Subsystem

-   **Lack of GPU Acceleration:** All graphics operations are performed in software by the CPU, limiting performance.
-   **Direct Framebuffer Access for Userspace:** User applications gain direct, unrestricted access to framebuffer memory, which simplifies development but offers no protection and requires all windowing system logic (composition, window management) to be implemented in user space.
-   **Internal GUI:** The existing kernel GUI toolkit (`gui.c`) is for internal use only and does not provide an API for user-space applications.

### Inter-Process Communication (IPC)

-   **Lack of Pipes:** The classic IPC mechanism "pipes" is not implemented.
-   **Lack of Explicit Shared Memory API:** Although threads within the same process share memory, an explicit API for creating and managing shared memory segments for different processes is not provided.

## 18.2. Unimplemented Subsystems

-   **USB Stack:** Support for USB devices is absent.
-   **AHCI Driver:** Support for modern SATA controllers is absent; only the older IDE driver is used.
-   **Full `libc`:** A full-fledged standard C library for user space is absent, forcing applications to either use minimal syscall wrappers or include their own implementation.
-   **Virtualization:** Currently, no hardware or software virtualization mechanisms are provided.

## 18.3. Design Compromises

-   **Simplicity and Readability:** Many decisions in KyroOS prioritize implementation simplicity and code readability, sometimes at the expense of performance, security, or functionality.
-   **Minimalism:** The goal was to create a working system from scratch, not a full-fledged commercial OS. This led to conscious omission of many complex features in the initial stages.
-   **Educational Focus:** The system serves as a learning project, which explains some simplifications and "TODO" comments in the code.

## 18.4. Known Issues

-   **Alpha Quality:** The system is in its alpha stage of development, which implies a high probability of bugs, instability, and potential unforeseen crashes.
-   **Non-POSIX Compliance:** The KyroOS API does not fully comply with the POSIX standard, complicating the porting of existing software.
-   **Low Performance:** Due to the lack of optimizations, advanced algorithms, and caching in some subsystems, performance may be low.
