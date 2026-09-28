# 15. Debugging and Logging

KyroOS provides various debugging and logging mechanisms to ensure stability, diagnose errors, and aid development.

## 15.1. Debug Interfaces

### Serial Port

The primary low-level interface for debugging is the serial port (COM1).
-   **Early Initialization:** Functions `serial_init()`, `serial_print()`, and `serial_print_hex()` allow outputting messages to the serial port at very early stages of kernel boot, even before graphical subsystems or the full logging system are available.
-   **Persistent Output:** All logging system (`klog`) messages are duplicated to the serial port. This is critically important for analyzing problems when the main display is not working or unavailable.

### QEMU Debugger (GDB)

KyroOS is actively developed and tested in the QEMU virtual environment. QEMU provides powerful capabilities for kernel debugging:
-   **GDB Integration:** When QEMU is run with `-s -S` flags (e.g., `qemu-system-x86_64 -cdrom kyroos.iso -serial stdio -s -S`), QEMU opens a GDB server.
-   **Remote Debugging:** This server can be connected to using the GNU Debugger (GDB) for step-by-step kernel code execution, setting breakpoints, and viewing registers and memory. This is an indispensable tool for low-level debugging.

### `epstein` Flag (Debug Mode)

In debug builds, there is an `epstein` flag (see `src/kernel/epstein.h`), which can be set to 1.
-   **Capabilities:** Activating this flag can provide additional debugging functions, such as the `panic <reason>` command in the system shell, allowing an artificial Kernel Panic to be triggered for testing the handler.
-   **Limitations:** This flag **should not be activated** in production builds, as it could introduce vulnerabilities or instability.

## 15.2. Logging

The KyroOS kernel provides a centralized logging system through the `klog()` function.

### `klog()`

-   **Log Levels:** `klog()` supports various message importance levels (`LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_PANIC`), allowing output filtering.
-   **Formatted Output:** Supports `printf`-like syntax for convenient message formatting.
-   **Output Streams:** `klog()` messages are simultaneously output to three streams:
    1.  **Serial Port:** For remote debugging and persistent recording.
    2.  **Framebuffer Console:** A text console displaying kernel messages is shown on the computer screen, supporting scrolling.
    3.  **In-Memory History:** The last 10 messages are stored in a circular buffer (`log_history`) in kernel memory.

### Framebuffer Console

Functions (`klog_putchar`, `klog_print_str`, `console_clear`) provide a text console on the graphical Framebuffer. It supports basic features:
-   Output of characters and strings.
-   Line wrapping.
-   Scrolling when the screen is full.

## 15.3. Crash Diagnostics

KyroOS includes a specialized mechanism for diagnosing critical kernel failures, known as Kernel Panic.

-   **`panic()` Function:** This is the central entry point for handling all unrecoverable kernel errors.
    -   When called, a brief message is first output to the serial port.
    -   Then `panic_screen_show()` is called, which takes over graphical output.
-   **Kernel Panic Screen:** This specialized graphical screen provides comprehensive information about the system's state at the time of the crash (detailed in **[17. Kernel Panic and Critical Kernel Errors](./kernel_panics.md)**):
    -   **Panic Message:** Passed to `panic()` and displayed prominently.
    -   **Register Dump:** Displays the values of all major CPU registers at the time of panic.
    -   **Stack Trace:** Reconstructs the sequence of function calls preceding the crash.
    -   **Page Fault Information:** For Page Fault exceptions, the error code is decoded, and the faulting address is displayed.
    -   **System Information:** OS version, CPU and memory data.
    -   **Log History:** The last 10 messages stored in `log_history` are displayed, allowing observation of events immediately preceding the crash.

All these measures enable the developer to obtain the most complete picture of what happened, significantly simplifying the debugging process.
