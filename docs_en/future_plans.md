# 19. Development Plans

KyroOS is currently in an active development phase. The plans outlined below reflect the most relevant directions for expanding functionality, improving performance, and enhancing system stability. These plans are based on current architectural compromises and identified limitations.

## 19.1. Kernel Architecture Expansion

### Multitasking and Synchronization

-   **Implementation of Synchronization Primitives:** Introduction of spinlocks, mutexes, and semaphores to ensure thread safety and prevent race conditions in a multithreaded environment.
-   **Full-fledged Blocking/Waking Mechanism:** Development of a robust mechanism for putting threads into a waiting state and subsequently waking them up based on events (e.g., I/O completion, data availability in a buffer).
-   **Support for Multi-core Processors (Symmetric Multiprocessing, SMP):** Adapting the kernel to run on multi-core systems, including rethinking synchronization and scheduling mechanisms.

### Memory Management

-   **Improved `fs_disk` Allocation:** Implementation of a free block bitmap for `fs_disk` to efficiently reuse freed disk space and reduce fragmentation.
-   **`KyroFS` Optimization:** Implementation of more efficient strategies for allocating and reallocating memory for files in `KyroFS`, for example, using block allocation or a copy-on-write mechanism.

### File System

-   **`fs_disk` Integration with VFS:** Adapting `fs_disk` to be used through the VFS interface, allowing it to be mounted into the file system hierarchy.
-   **Hierarchical `fs_disk` Structure:** Adding directory support to `fs_disk` to create a full file hierarchy.
-   **Access Control Model:** Development and implementation of an access control system (user, group, others; read, write, execute) at the VFS level and for specific FS implementations.
-   **`fs_disk` Data Caching:** Implementation of a buffer cache for `fs_disk` data blocks with eviction strategies (e.g., LRU) to improve disk operation performance.
-   **Support for Other File Systems:** Adding drivers for widely used file systems such as FAT32 or ext2/3/4.

### Network Stack

-   **Network Core Decoupling:** Separating the general network core (`net_init`, `net_register_device`) from specific drivers (e.g., E1000) to improve modularity and simplify the addition of new network drivers.
-   **Completion of TCP Implementation:** Refinement of the TCP stack for full support of all states, recovery mechanisms, window management, and checksums.
-   **Addition of New Network Drivers:** Introduction of drivers for other popular network adapters (e.g., VirtIO Net, Realtek RTL8139).

### Graphics Subsystem

-   **Development of User-Space Windowing System:** Creation of a full-fledged user library for rendering windows, widgets, and managing composition based on direct framebuffer access.
-   **GPU Hardware Acceleration Support:** (Long-term perspective) Research and integration of drivers for graphics processors with hardware acceleration support.

## 19.2. User Space Capabilities Expansion

### Full-Featured KyroOS Package Manager (KPM)
-   **KPM Development:** Developing the KPM package manager to a full-featured state, including:
    -   **Dependency Resolution:** Automatic identification and installation of necessary package dependencies.
    -   **Package Installation and Uninstallation:** Reliable mechanisms for installing, updating, and uninstalling software.
    -   **Repository Integration:** Ability to work with remote or local package repositories.
    -   **Package Format:** Documenting and standardizing the `.kpkg` (KyroOS Package) format, considering metadata, checksums, and structure.

-   **Full-fledged `libc`:** Development or porting of a more complete standard C library, providing not only system call wrappers but also a broader set of standard functions.
-   **Dynamic Linking:** Introduction of support for dynamic libraries (shared libraries), which will reduce the size of executables and simplify component updates.
-   **Improved Shell:** Evolution of the system shell (`kyroshell`) with support for command history, autocompletion, aliases, and scripts.
-   **Additional Utilities:** Development and integration of a wider set of standard Unix-like utilities (e.g., `grep`, `sed`, `awk`).

## 19.3. System Stability and Debugging Improvements

-   **Enhanced Error Handling:** Re-evaluation of the current kernel error handling strategy to reduce the number of Kernel Panics in cases where softer recovery is possible (e.g., terminating a problematic process instead of panicking the entire kernel).
-   **Crash Dump Mechanisms:** Implementation of saving the full kernel state (memory dump) during a Kernel Panic for subsequent offline debugger analysis.
-   **Profiling and Monitoring Tools:** Development of tools for measuring performance and identifying bottlenecks in the kernel and applications.
-   **Testing:** Introduction of unit tests and integration tests for key kernel components.
