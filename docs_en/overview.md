# KyroOS: Technical Specification

## 1. Introduction

### 1.1. OS Purpose

KyroOS is an experimental, self-developed general-purpose operating system for the 64-bit x86-64 architecture. It is not based on existing kernels such as Linux or BSD. The system is designed from scratch for educational purposes to research and practically implement key concepts of modern operating systems.

### 1.2. Scope of Application

The primary application area for KyroOS is educational and research. The system serves as a platform for studying low-level programming, kernel architecture, driver development, and system utilities.

KyroOS is not intended for production use. The project status is **alpha**, which implies the presence of known and unknown bugs, incomplete functionality, and potential instability.

### 1.3. Design Goals

The following engineering goals were set during the development of KyroOS:

*   **Simplicity and Code Readability:** The architecture and code should be simple enough for easy study and understanding of key mechanisms. Preference is given to clear and straightforward implementations over overly complex optimizations.
*   **Extensibility:** The architecture should allow for relatively easy addition of new modules, drivers, and system calls.
*   **Adherence to Modern Concepts:** The implementation should reflect standard approaches used in modern 64-bit OS (virtual memory, preemptive multitasking, separation of kernel and user space).
*   **Self-sufficiency:** The system should be minimally dependent on external libraries and runtime environments at the kernel level.

### 1.4. Architectural Principles

*   **Monolithic Kernel:** All core OS services (process management, memory, file system, drivers) operate within a single kernel address space for improved performance.
*   **Privileged Kernel and Unprivileged User Space:** Clear separation between kernel mode (Ring 0) and user mode (Ring 3) to ensure system protection and stability.
*   **Preemptive Multitasking:** A timer-based scheduler preempts tasks, providing pseudo-parallel execution of multiple processes.
*   **Hardware Abstraction:** Interaction with hardware is done through standardized driver interfaces.

## 2. General Architecture

### 2.1. Kernel Type

The KyroOS kernel is **monolithic** with support for Loadable Kernel Modules (LKM).

Key subsystems, including the scheduler, VFS, network stack, and device drivers, are compiled as a single binary file and operate within a single address space (Ring 0). This provides high performance due to direct function calls instead of the slower IPC mechanisms characteristic of microkernels. LKM support allows dynamic (during system operation) loading and unloading of code into the kernel, for example, to add support for new hardware.

### 2.2. Supported CPU Architectures

Currently, KyroOS supports only the **x86-64** architecture.

The system uses 64-bit long mode, a 4-level page table structure (PML4), and registers specific to x86-64. Kernel code is compiled with `-m64` and `-march=x86-64` flags. The kernel does not use MMX and SSE extensions for simplicity, but they can be used in user space.

### 2.3. Processor Operating Modes

KyroOS uses two privilege levels of the x86-64 architecture:

*   **Kernel Mode (Supervisor Mode, Ring 0):** All kernel code, including interrupt and system call handlers, and drivers, executes in this mode. Code in Ring 0 has full access to the entire address space and all processor instructions.
*   **User Mode (Ring 3):** All user applications (e.g., shell, utilities) execute in this mode. Code in Ring 3 is isolated: it only has access to its own virtual address space and cannot execute privileged instructions. Interaction with the kernel occurs exclusively through the system call mechanism.

### 2.4. Multitasking Model

KyroOS implements **Preemptive Multitasking** based on time-slicing.

*   **Entities:** The system operates with the concepts of **processes** and **threads** (although the current implementation may be closer to a "one process — one thread" model).
*   **Scheduler:** A "Round-Robin" type scheduler is used. Each active thread is allocated a time quantum. Upon expiration of the quantum, the system timer generates an interrupt (IRQ), whose handler invokes the scheduler.
*   **Context Switching:** The scheduler selects the next thread to execute and performs a context switch. This process involves saving the register state of the current thread and loading the state of the next thread.
*   **States:** Threads can be in various states (e.g., `RUNNING`, `READY`, `BLOCKED`). Blocking occurs when waiting for events such as I/O completion or release of a synchronization resource.
