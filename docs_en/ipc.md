# 8. IPC and Synchronization

## 8.1. IPC Mechanisms

Inter-Process Communication (IPC) in KyroOS is in its early stages of development. Currently, one primary mechanism is implemented.

### 8.1.1. Sockets

Sockets are the primary means of inter-process communication in KyroOS. The implementation of the network stack, including TCP and UDP, allows processes to exchange data both over the network and locally via the loopback interface. This is a standard and flexible mechanism for implementing client-server architectures.

### 8.1.2. Shared Memory

**Assumed Design:** The system lacks an explicit API for creating shared memory segments (like `shm_open` in POSIX). However, the VMM architecture allows for this mechanism. Threads belonging to the same logical process (i.e., using the same `pml4` address space) implicitly share all memory by default, which is a form of implicit shared memory.

### 8.1.3. Pipes

Pipes are currently **not implemented**. Their addition is planned for future versions.

## 8.2. Synchronization Primitives

At the current stage of development, KyroOS lacks high-level synchronization primitives such as mutexes, semaphores, or spinlocks.

### 8.2.1. Interrupt Disabling

The only synchronization mechanism used within the kernel is the global disabling and enabling of interrupts (`disable_interrupts()` / `enable_interrupts()`).

-   **Application:** This method is used to protect very short critical sections where it is necessary to ensure the atomicity of operations relative to hardware interrupts. Examples include modifying scheduler queues or the global event queue.
-   **Limitations:**
    -   This is a very "heavy" and coarse-grained mechanism that halts all system activity, including timer ticks.
    -   It **does not solve the problem of synchronization on multi-core systems** (which are not currently supported), as `cli` only affects the core where it was called.
    -   This mechanism cannot be used for long waits for a resource, as it completely "freezes" the system.

### 8.2.2. Thread Blocking

**Assumed Design:** The `THREAD_BLOCKED` thread state exists, and the scheduler can handle it (it does not select blocked threads for execution). However, based on comments in the code (`TODO: proper thread blocking/unblocking`), a full-fledged mechanism that would move a thread to the `BLOCKED` state when waiting for a resource and wake it up (`READY`) when it is released, is **not yet implemented**.

## 8.3. Event-Driven Model

KyroOS implements a simple event-driven model primarily used for input handling.

### Event Queue

-   **Architecture:** Implemented as a global, static **circular buffer** (`Event Queue`) of fixed size (256 events).
-   **Event Structure (`event_t`):** Each event contains a type (`EVENT_KEY_DOWN`, `EVENT_MOUSE_MOVE`, etc.) and up to three integer data fields (e.g., key scancode or mouse coordinates).
-   **Producers:** Interrupt handlers from input devices (keyboard, mouse) are event producers. Upon receiving data from the device, they form an `event_t` and push it onto the tail of the queue using `event_push()`.
-   **Consumers:** User applications are consumers. They can request events from the head of the queue using the `SYS_INPUT_POLL_EVENT` system call, which internally calls `event_pop()`.
-   **Synchronization:** Access to the queue (modifying `head`/`tail` pointers and `event_count`) is protected by `disable_interrupts()`, ensuring thread safety in the context of a single-core system.
