# 5. Memory Management

Memory management in KyroOS is built upon a two-tier model, standard for modern operating systems: the Physical Memory Manager (PMM) manages the actual RAM resources, and the Virtual Memory Manager (VMM) provides isolated address spaces for each process.

## 5.1. Physical Memory (PMM)

The Physical Memory Manager (`PMM`) is responsible for accounting, allocating, and freeing pages of physical RAM.

### Strategy

The PMM uses a **bitmap** to track the state of every physical page in the system. Each 4KB page corresponds to one bit in the bitmap: 1 means the page is occupied, 0 means it's free.

### Initialization (`pmm_init`)

1.  During boot, the kernel receives a `memory map` from the Limine bootloader, which lists all available RAM ranges and their types (usable, reserved, ACPI, etc.).
2.  The PMM finds the highest memory address and calculates the total RAM, as well as the size required for the bitmap itself.
3.  It then locates the first sufficiently large *free* area in the memory map and places the bitmap there.
4.  Initially, the entire bitmap is filled with ones (all memory is considered used).
5.  Next, the PMM iterates through the memory map again and clears the bits for pages marked as `USABLE` by Limine (available for use).
6.  Finally, the bits corresponding to the pages where the bitmap itself resides are set to 1 again to protect it from being overwritten.

### Allocation and Deallocation

-   **`pmm_alloc_page()`:** Performs a linear search in the bitmap, starting from the last successful allocation. Upon finding the first unset bit (0), it sets it to 1 and returns the corresponding physical address of the page. This is a simple "next-fit" algorithm implementation.
-   **`pmm_free_page(address)`:** Calculates the page index from its physical address and clears the corresponding bit in the bitmap.

## 5.2. Virtual Memory (VMM)

The Virtual Memory Manager (`VMM`) is responsible for creating and managing virtual address spaces. This is a key mechanism for ensuring process isolation and memory protection.

### Architecture

KyroOS uses a **4-level paging model**, standard for the x86-64 architecture. Each virtual address space is described by a hierarchy of four tables:
1.  Page Map Level 4 (PML4) - Level 4 table (root)
2.  Page Directory Pointer Table (PDPT) - Page Directory Pointer Table
3.  Page Directory (PD) - Page Directory
4.  Page Table (PT) - Page Table

Each table contains 512 64-bit entries. Each entry in the PT points to a physical 4KB page.

### Address Space

The 64-bit virtual address space is divided into two halves:
-   **Lower Half (User Space):** Addresses from `0x0000000000000000` to `0x00007FFFFFFFFFFF`. This part is unique to each process and contains its code, data, heap, and stack.
-   **Upper Half (Kernel Space):** Addresses from `0xFFFF800000000000` onwards. This part is identical for all processes and contains the kernel's code and data, as well as a Higher-Half Direct Map (HHDM) of all physical memory.

### Higher-Half Direct Map (HHDM)

All physical memory is mapped into the upper half of the virtual address space with a constant offset (`hhdm_offset`). This allows the kernel to have direct access to any physical page by simply adding this offset to its physical address. The functions `p_to_v()` (physical-to-virtual) and `v_to_p()` (virtual-to-physical) are used for such conversions.

### Creating an Address Space (`vmm_create_address_space`)

When creating a new process:
1.  The PMM allocates one page for the new PML4 table.
2.  The lower half of this table (the first 256 entries), responsible for User Space, is zeroed out.
3.  The **upper half** (the last 256 entries), responsible for Kernel Space, is **copied** from the kernel's PML4 table.
Thus, the kernel is always mapped into every process, which makes handling system calls and interrupts very efficient, as it does not require switching address spaces.

## 5.3. Allocators

### Kernel Heap (`kmalloc`/`kfree`)

For dynamic memory allocation within the kernel (e.g., for creating data structures), a heap allocator is used.

-   **Strategy:** The implementation is based on the classic allocator from the K&R book. It uses a **linked list of free memory blocks**.
-   **`kmalloc()`:** When a memory allocation request is made, the allocator searches the `freelist` for the first sufficiently sized block ("first-fit"/"next-fit" style). If the block is larger than requested, it is split into two parts.
-   **`kfree()`:** The freed block is added back to the free list. The allocator also performs **coalescing**: if the freed block is adjacent to another free block, they are merged into one larger block to combat fragmentation.
-   **`morecore()`:** If `kmalloc` cannot find a suitable block, it calls an internal function `morecore`, which requests one or more new physical pages from the PMM, maps them into the kernel's virtual address space, and adds this new large chunk of memory to the free list.

## 5.4. Memory Protection

Memory protection is provided at the hardware level by the processor and configured via the VMM.

-   **Process Isolation:** Each process has its own PML4 table, so it cannot access the data of other processes. All its virtual addresses ultimately refer to the physical pages allocated to it by the PMM.
-   **Kernel Protection from User Space:** In the page table entries describing kernel memory, the `Supervisor` flag is set. Any attempt to access these pages from user space (Ring 3) will trigger a General Protection Fault exception.
-   **Write and Execute Protection:** For each page, flags can be set:
    -   **`PAGE_WRITE`:** Determines whether writing to the page is allowed. Program code typically resides on "read-only" pages.
    -   **`PAGE_NO_EXEC` (NX Bit):** Prohibits code execution from pages marked with this flag (e.g., from the stack or heap), which is an important security measure against "buffer overflow" type attacks.
