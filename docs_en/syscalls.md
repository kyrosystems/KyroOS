# 7. System Calls

System calls (syscalls) are the primary interface through which user-space applications (Ring 3) request the kernel to perform privileged operations, such as file manipulation, memory management, or device interaction.

## 7.1. System Call Mechanism

The system call mechanism in KyroOS uses a software interrupt.

1.  **Initiation:** A user application loads the system call number and its arguments into registers according to the ABI, then executes the `int 0x80` instruction.
2.  **Transition to Kernel:** The processor generates an interrupt with vector 128 (0x80). This triggers a switch from Ring 3 to Ring 0 and transfers control to the kernel's interrupt handler.
3.  **Low-Level Handler (`isr128`):** The assembly stub associated with vector 128 performs the following actions:
    *   Saves the full register context of the user thread onto its kernel stack.
    *   Calls the high-level C dispatcher `syscall_handler()`, passing it a pointer to the saved registers.
4.  **Dispatcher (`syscall_handler`):**
    *   Extracts the system call number from the saved `rax` register.
    *   Uses this number as an index into the global `syscall_table` to find the address of the function implementing the system call.
    *   If a handler is found, it is called. It is also passed a pointer to the `struct registers`.
5.  **Execution:** The specific handler (e.g., `sys_write`) extracts its arguments from the saved registers (`rdi`, `rsi`, `rdx`, etc.), performs the necessary work, and writes the result (return value) back into the `rax` field of the `registers` structure on the stack.
6.  **Return to User Space:** After the C handlers complete their work, control returns to `isr128`. It restores all registers from `struct registers` and executes the `iretq` instruction. This instruction atomically returns control to the user application, restoring its registers (including `rax` with the result) and switching the processor back to Ring 3.

## 7.2. ABI (Application Binary Interface)

The Calling Convention for system calls in KyroOS is based on the System V AMD64 ABI standard.

| Register | Purpose                                      |
| :------- | :------------------------------------------- |
| `rax`    | System call number (input), Return value (output) |
| `rdi`    | 1st argument                                 |
| `rsi`    | 2nd argument                                 |
| `rdx`    | 3rd argument                                 |
| `r10`    | 4th argument                                 |
| `r8`     | 5th argument                                 |
| `r9`     | 6th argument                                 |

A negative value in `rax` after the call usually indicates an error.

## 7.3. System Call Table

In the kernel (`src/kernel/syscall.c`), a static `syscall_table` is defined — an array of 256 function pointers.

```c
static void (*syscall_table[256])(struct registers *regs);
```

The `syscall_init()` function populates this table, mapping system call numbers (macros `SYS_*` from `src/include/syscall.h`) to their C implementations. If an application requests a non-existent syscall, the dispatcher does not find a handler in the table and simply returns control without performing any action.

### Main System Calls:

| Number | Name                  | Description                                            |
| :----- | :-------------------- | :----------------------------------------------------- |
| 0      | `SYS_EXIT`            | Terminate the current thread.                          |
| 1      | `SYS_WRITE`           | Write data to a file or standard output.               |
| 2      | `SYS_OPEN`            | Open or create a file.                                 |
| 3      | `SYS_CLOSE`           | Close a file descriptor.                               |
| 4      | `SYS_READ`            | Read data from a file.                                 |
| 5      | `SYS_STAT`            | Get file information.                                  |
| 6      | `SYS_MKDIR`           | Create a directory.                                    |
| 10     | `SYS_SOCKET`          | Create an endpoint for communication (socket).         |
| 17     | `SYS_GET_TICKS`       | Get the number of system timer ticks.                  |
| 24     | `SYS_EXEC`            | Load and execute a program (simplified version).       |
| 25     | `SYS_GFX_GET_FB_INFO` | Get framebuffer information.                           |
| 26     | `SYS_INPUT_POLL_EVENT`| Poll the input event queue.                            |

*(For a complete list, see `src/include/syscall.h`)*

## 7.4. Usage Examples (C-like)

Since system calls are executed via assembly instructions, wrapper functions are created in the user-space standard library (`libc`).

**Example 1: Outputting the string "Hello, World!" to standard output (stdout = 1).**

```c
// Code in userspace/lib/libc/stdio.c (example)
#include <unistd.h>
#include <string.h>

void puts(const char* str) {
    // SYS_WRITE = 1
    // 1st argument (fd) = 1 (stdout)
    // 2nd argument (buf) = str
    // 3rd argument (size) = strlen(str)
    syscall(SYS_WRITE, 1, str, strlen(str));
    syscall(SYS_WRITE, 1, "\n", 1); // Add newline
}

// Code in application
int main() {
    puts("Hello, World!");
    return 0;
}
```
Here, `syscall()` is an assembly wrapper function that loads arguments into registers and executes `int 0x80`.

**Example 2: Getting the current time in ticks.**
```c
// Code in userspace/lib/libc/time.c (example)
uint64_t get_ticks() {
    // SYS_GET_TICKS = 17. No arguments.
    return syscall(SYS_GET_TICKS);
}
```
Here, `syscall(SYS_GET_TICKS)` will load `17` into `rax`, execute `int 0x80`, and after returning from the kernel, will return the value that the kernel placed in `rax`.
