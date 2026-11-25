# 10. Driver Model

KyroOS uses a hybrid driver model, combining dynamic discovery for modern buses with static initialization for legacy hardware.

## 10.1. Driver Architecture

The modern part of the driver model is based on three components:
-   **Device (`device_t`):** A structure representing a single hardware device discovered in the system. It contains identification information (e.g., Vendor/Device ID for PCI devices) and a pointer to the driver bound to it.
-   **Driver (`driver_t`):** A structure representing a driver. It contains the driver's name and pointers to key functions:
    -   `probe()`: Checks if this driver can work with the given device.
    -   `attach()`: Initializes the device and "attaches" the driver to it.
    -   `detach()`: Releases resources and detaches the device.
-   **Device Manager (`deviceman`):** A central component that acts as a registry. It stores a list of all drivers registered in the system and a list of all discovered devices.

## 10.2. Device Initialization

The initialization process for modern PCI devices goes through several stages:

1.  **Discovery:** During kernel boot, the `pci_check_all_buses()` function is called, which scans the PCI bus. For each found device, it creates a `device_t` structure and registers it with the Device Manager using `deviceman_register_device()`.
2.  **Driver Registration:** Each driver for PCI devices (e.g., `e1000.c`) has an initialization function (`e1000_driver_init()`) that is called from `kmain`. This function registers the driver itself (its `driver_t` structure) with the Device Manager via `deviceman_register_driver()`.
3.  **Matching (Probe/Attach):** After all devices are discovered and all drivers are registered, the `deviceman_probe_devices()` function is called. It executes the following logic:
    -   For each device from the list of discovered devices...
    -   ...it iterates through all drivers from the list of registered drivers.
    -   `driver->probe(device)` is called. If the function returns `true` (the driver is a match),...
    -   ...then `driver->attach(device)` is called. If `attach` completes successfully, the device and driver are linked, and the process moves to the next device.

### Initialization of Legacy Devices

For non-discoverable hardware, such as the PS/2 keyboard or system timer (PIT), a simpler static model is used. Their initialization functions (e.g., `keyboard_init()`) are called directly from `kmain`. These functions manually configure I/O ports and register their interrupt handlers for the corresponding IRQ numbers.

## 10.3. Supported Buses

-   **PCI (Peripheral Component Interconnect):** The primary bus for discovering and configuring modern devices such as network cards and disk controllers. The kernel contains a full-fledged PCI scanner.
-   **ISA (Legacy):** Older devices, such as the keyboard, mouse, and timer, are managed directly through I/O Ports.

## 10.4. Interaction with the Kernel

Drivers are an integral part of the kernel and interact with other subsystems in various ways:

-   **Registration with Device Manager:** As described above, this is the primary way for PCI drivers to announce themselves.
-   **Interrupt Handler Registration:** Drivers call `register_irq_handler()` to bind their function to a specific IRQ. This function will be called each time a hardware interrupt occurs from the device.
-   **Memory Allocation:** Drivers use `kmalloc()` to allocate memory for their internal structures and `pmm_alloc_page()` to allocate pages for DMA buffers.
-   **VFS Interaction:** Block device drivers (e.g., IDE) can provide their `read`, `write`, `ioctl` functions and register `vfs_node_t` in the file system (e.g., `/dev/hda`). This allows user space to interact with the device through standard file operations.
-   **Network Stack Interaction:** Network drivers (e.g., `e1000`) register themselves with the network subsystem (`net_register_device()`), providing a function to send packets. Upon receiving a packet, the driver's interrupt handler passes the data up the network stack (e.g., to the IP or ARP handler).
-   **Event Queue:** Input device drivers (keyboard, mouse) do not interact directly with VFS. Instead, they generate events (`event_t`) and place them into a global, centralized **event queue**, from which applications can read them via the `SYS_INPUT_POLL_EVENT` system call.
