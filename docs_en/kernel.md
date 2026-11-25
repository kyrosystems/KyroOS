# 4. Kernel

## 4.1. Kernel Structure

The KyroOS kernel has a **monolithic** architecture. This means that all core components — memory management, task scheduler, file system, network stack, and device drivers — reside in a single address space and execute at privilege level Ring 0.

### Source Code Structure

The kernel's source code is organized into the following key directories:
- `src/kernel/`: Primary logic for kernel subsystems (C files).
- `src/boot/`: Low-level assembly code for booting and context switching.
- `src/include/`: Header files for all subsystems, defining APIs and data structures.

### Core Components

The kernel consists of tightly integrated, but logically separated components:
-   **Physical Memory Manager (PMM):** Responsible for tracking and allocating physical memory pages.
-   **Virtual Memory Manager (VMM):** Manages process address spaces, page tables, and ensures memory isolation.
-   **Scheduler:** Implements multitasking by switching context between threads.
-   **System Calls (Syscall):** The mechanism through which user applications request services from the kernel.
-   **Virtual File System (VFS):** An abstract layer for interacting with various file systems.
-   **Drivers:** Modules controlling specific hardware devices (keyboard, timer, IDE, E1000).

## 4.2. Task Scheduler

The scheduler is responsible for implementing preemptive multitasking.

### Algorithm

The classic **Round-Robin** algorithm is used. Each thread ready to execute is allocated a specific time quantum. If a thread does not complete or block within this quantum, it is forcibly preempted, and the processor is given to the next thread in the queue.

### Time Quantum

The system timer is configured to generate an interrupt at **100 Hz**. This means the scheduler is invoked every **10 milliseconds**, which serves as the time quantum for each thread.

### Data Structure

All threads in the system are organized as a **singly-linked circular list** (`ready_queue`). The `ready_queue` pointer points to the "tail" of the queue. Accordingly, `ready_queue->next` is the "head" of the queue — the next candidate for execution.

### Operation Logic (`schedule()`)

The `schedule()` function is invoked by the timer interrupt handler. Its logic is as follows:
1.  Interrupts are disabled to ensure atomic operation.
2.  The scheduler searches the circular list for the next `THREAD_READY` thread.
3.  During the search, it performs "garbage collection": if a `THREAD_DEAD` thread is encountered, the scheduler frees all its allocated resources (kernel stack, user stack, page tables) and removes it from the list.
4.  If the currently executing thread (`current_thread`) transitions to the `THREAD_READY` state (its time quantum has expired), it remains in the list for subsequent execution.
5.  The selected next thread for execution is marked as `THREAD_RUNNING`.
6.  If a new thread is chosen for execution that is different from the previous one, the low-level `thread_switch()` function is called to perform a context switch.

## 4.3. Processes and Threads

In KyroOS, the primary unit of scheduling is a **thread** (`thread_t`). The concept of a **process** is a logical abstraction and is not represented by a separate structure. A process is a collection of one or more threads that execute within a shared virtual address space and share common resources.

### `thread_t` Structure
Key fields of the `thread_t` structure (`src/include/thread.h`):
- `id`: Unique thread identifier.
- `state`: Current state (`THREAD_RUNNING`, `THREAD_READY`, `THREAD_BLOCKED`, `THREAD_DEAD`).
- `stack`: Pointer to the top of the kernel stack for this thread.
- `user_stack_base`: Base address of the userspace stack.
- `rsp`: Saved value of the `RSP` register (kernel stack pointer) at the moment the thread was preempted.
- `pml4`: Pointer to the top-level page table (PML4), which defines the thread's virtual address space. Threads within the same process share the same `pml4`.
- `fd_table`: Its own file descriptor table.
- `next`: Pointer to the next thread in the scheduler's circular list.

## 4.4. Context Switching

Context switching is a low-level operation of saving the state of one thread and restoring the state of another. It is implemented in the assembly function `thread_switch` (`src/boot/switch.asm`).

### `thread_switch(old_thread, new_thread)` Procedure

1.  **Save `old_thread` Registers:**
    *   Callee-saved registers (`rbp`, `rbx`, `r12`, `r13`, `r14`, `r15`) are saved onto the current kernel stack.
    *   The instruction pointer (`rip`) is implicitly saved onto the stack by the `call` instruction that invoked the scheduler.
2.  **Save Stack Pointer:** The current value of the `rsp` register is saved into the old thread's structure: `old_thread->rsp = rsp`.
3.  **Address Space Switch:**
    *   The `pml4` pointers of the old and new threads are compared.
    *   **If the pointers differ**, it indicates a switch between different processes. In this case, the physical address of the new thread's `pml4` is loaded into the `CR3` register. This atomically switches the entire virtual address space.
    *   **If the pointers are the same**, switching `CR3` is not required, as the threads belong to the same process.
4.  **Restore `new_thread` Context:**
    *   The stack pointer value is restored from the new thread's structure: `rsp = new_thread->rsp`.
    *   The callee-saved registers (`rbp`, `rbx`, etc.) are popped from the new thread's stack in reverse order.
5.  **Function Return:** The `ret` instruction pops the previously saved instruction pointer (`rip`) of the new thread from the stack and jumps to that address. From this point, the new thread begins execution from where it was preempted.
