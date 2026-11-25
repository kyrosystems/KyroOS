# 3. System Boot

## 3.1. Boot Chain

KyroOS uses the modern [Limine](https://github.com/limine-bootloader/limine) bootloader, which supports both legacy BIOS systems and modern UEFI systems. The boot process consists of several sequential stages, transferring control from the computer's firmware to the operating system kernel.

**General Sequence:**

1.  **Firmware (BIOS/UEFI):** Performs basic hardware initialization (Power-On Self-Test, POST) and, according to its settings, locates the boot sector on the boot medium (e.g., ISO image).
2.  **Limine Bootloader:** The firmware transfers control to the Limine bootloader. Limine scans the file system of the medium, locates and reads the `limine.conf` configuration file.
3.  **Kernel Loading:** Following the instructions from `limine.conf`, Limine loads the main kernel file (`/boot/kernel.elf`) and the configured module files (e.g., userspace utilities) into RAM.
4.  **Transfer Control to Kernel:** Limine prepares the environment (sets up 64-bit mode, forms a memory map, etc.) and transfers control to the kernel's entry point, defined in the ELF file.

## 3.2. Kernel Boot Stages

The kernel's initialization process itself is divided into two main stages: low-level assembly preparation and high-level C subsystem initialization.

### Stage 1: Low-Level Initialization (`_start` in `src/boot/boot.asm`)

Immediately after receiving control from Limine, the processor begins executing code from the `_start` label. At this stage, critically important initial actions are performed:

1.  **Disable Interrupts:** The very first instruction is `cli`. This prevents any hardware interrupts from occurring until the system is ready to handle them (i.e., until the IDT and handlers are set up).
2.  **Stack Setup:** The stack pointer (`rsp`) is set up to a pre-allocated memory region in the BSS section. This is necessary for correct function call operation.
3.  **Call C Code:** The `call kmain_x64` instruction is executed, which transfers control to the main kernel initialization function written in C.

At this point, the assembly code's work is complete. Its sole task is to create the minimally required environment to launch C functions.

### Stage 2: High-Level Initialization (`kmain_x64` in `src/kernel/kernel.c`)

The `kmain_x64` function is the entry point to the high-level part of the kernel and performs the sequential initialization of all key subsystems. The order of initialization is strictly defined and critically important for the correct system startup.

1.  **Logging Initialization (`log_init`):** The logging subsystem is started, allowing debug information to be output via the serial port.
2.  **Framebuffer Initialization (`fb_init`):** Based on information received from Limine, the video buffer is configured for displaying graphics and text on the screen.
3.  **Memory Management Initialization:**
    *   **PMM (`pmm_init`):** The Physical Memory Manager is initialized, which receives the available memory map from Limine and creates a structure for tracking free and used pages.
    *   **Heap (`heap_init`):** A kernel heap for dynamic memory allocation (`kmalloc`, `kfree`) is created based on the PMM.
    *   **VMM (`vmm_init`):** The Virtual Memory Manager is initialized. Page tables (PML4) are created and loaded for the kernel's address space.
4.  **Processor Structure Configuration:**
    *   **GDT (`gdt_init`):** A new Global Descriptor Table is loaded, containing segments for kernel code and data and user space.
    *   **IDT (`idt_init`):** The Interrupt Descriptor Table is loaded with CPU exception handlers and hardware interrupt handlers.
    *   **TSS (`tss_init`):** The Task State Segment is initialized, used for stack switching during privilege level changes.
5.  **Basic Driver Initialization:**
    *   **Keyboard (`keyboard_init`):** The driver for handling keyboard input is configured.
    *   **Timer (`timer_init`):** The system timer interrupt is programmed (typically at 100 Hz), which will be used by the scheduler.
6.  **File System Initialization:**
    *   **VFS (`vfs_init`):** The Virtual File System layer is started, providing a unified API for file operations.
    *   **KyroFS (`kyrofs_init`):** The root file system (in-memory) is initialized and mounted.
    *   **Module Loading:** Files loaded by Limine as modules are placed into the KyroFS file system using VFS (e.g., in the `/bin/` directory).
7.  **Device and Driver Initialization:**
    *   **Device Manager (`deviceman_init`):** The device manager is started.
    *   **PCI (`pci_check_all_buses`):** The PCI bus is scanned to discover connected devices.
    *   **Network Stack:** All layers of the network stack are sequentially initialized (E1000, ARP, IP, UDP, TCP, DHCP).
8.  **Multitasking Startup:**
    *   **Threads (`thread_init`):** The thread management subsystem is initialized.
    *   The first kernel thread is created, which begins executing the main function of the user shell (`shell_main`).
9.  **Enable Interrupts:** The `sti` instruction is executed. From this point, the system becomes fully interactive: the processor begins responding to timer interrupts (triggering the scheduler) and input devices.

After executing `sti`, the `kmain_x64` function effectively completes its work, and system control fully transfers to the scheduler and interrupt handlers.
