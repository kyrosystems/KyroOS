# 9. File System

The File System (FS) in KyroOS has a two-tier architecture, consisting of an abstract VFS layer and concrete FS implementations. Currently, two implementations coexist in the system: a modern, hierarchical in-memory FS (`KyroFS`) and an older, flat on-disk FS (`fs_disk`).

## 9.1. VFS Architecture (Virtual File System)

VFS is an abstract layer that provides a unified interface for working with files and directories to all kernel components and user space. It allows supporting multiple different file systems, abstracting away the details of their implementation.

### `vfs_node_t`

The central structure of the VFS is `vfs_node_t` (analogous to `inode` in Unix). This structure represents any object in the file hierarchy (a file or a directory) and contains:
-   Basic metadata: name, flags, size.
-   A set of **function pointers** (`read`, `write`, `open`, `finddir`, etc.).

It is through these pointers that the abstraction is implemented. When the kernel calls, for example, `vfs_read()`, the VFS simply passes the call to the `read` function pointer attached to the file's `vfs_node_t`. This handler function is provided by the specific file system driver that owns the file (e.g., KyroFS or fs_disk).

### Registration and Mounting

-   **Registration:** File system drivers can register themselves with the VFS during kernel initialization, providing their name (e.g., "kyrofs") and a mount function.
-   **Mounting:** The `mount` system call allows "attaching" a new file system (e.g., from a disk partition) to an existing directory in the VFS tree. The VFS finds the appropriate driver by name and calls its mount function, which prepares the FS for operation and links it to the VFS tree at the specified mount point.

## 9.2. File System Implementations

### KyroFS (In-Memory Filesystem)

KyroFS is a simple, hierarchical file system that operates entirely in RAM.
-   **Purpose:** Used as the **root file system (`/`)**. During boot, it is populated with system directories (`/bin`, `/etc`) and executables, pre-loaded by the Limine bootloader.
-   **Structure:**
    -   **Directories:** Represented as a linked list of `vfs_node_t`, where each list node is a file or subdirectory.
    -   **Files:** The content of each file is stored in a dynamically allocated buffer on the kernel heap. When data is written, if the buffer overflows, it is reallocated with a larger size.
-   **Advantages and Disadvantages:**
    -   **(+)** Very high performance, as there are no disk accesses.
    -   **(-)** Not persistent: all changes are lost upon reboot.

### fs_disk (On-Disk Filesystem)

`fs_disk` is a very simple, **flat** file system for disk partitions.
-   **Architecture:** Currently **not integrated with VFS**. It represents an older implementation with its own API (`fs_create_file`, `fs_read_file`) that does not use `vfs_node_t`.
-   **On-disk Structure:**
    -   **Block 0:** Superblock with a "KYRO" signature and basic FS geometry information.
    -   **Blocks 1..N:** File table — a contiguous array of `fs_file_entry_t` entries.
    -   **Remaining blocks:** Data area.
-   **Limitations:**
    -   **Flat Structure:** No support for directories. All files reside in a single root namespace.
    -   **Allocation:** Uses a primitive "next free block" counter. Freed blocks are not reused. Files must occupy contiguous blocks, leading to severe external fragmentation.
    -   **Maximum 32 files** for the entire FS.

## 9.3. Metadata

-   **VFS (`struct stat`):** The VFS provides a standard `stat` structure, containing basic file information: size (`st_size`), mode (`st_mode` — file type), and inode number (`st_ino`).
-   **KyroFS:** Metadata is stored directly in `vfs_node_t` structures in RAM.
-   **fs_disk (`fs_file_entry_t`):** Metadata is stored in the file table on disk and includes the filename, its size, flags, and the starting data block number.

## 9.4. Caching

-   **KyroFS:** The concept of caching is not applicable, as the FS is already in memory.
-   **fs_disk:** A **file table cache** is implemented. Upon mounting, the entire file table is read from disk into memory. All file creation/deletion operations are performed on this in-memory copy. The updated table is written back to disk only when unmounting. **Data block caching is absent**; all read and write operations go directly to disk.

## 9.5. Access Rights

Currently, KyroOS **does not implement** a full-fledged access control model (read/write/execute for user/group/others). The only distinction made by the system is the file type (regular file or directory), information about which is stored in the `st_mode` flag.
