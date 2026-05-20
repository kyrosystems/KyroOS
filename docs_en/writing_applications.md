# Writing Applications for KyroOS

Developing applications specifically for KyroOS requires an understanding of its native environment and available APIs. This guide outlines how to structure, write, and build your applications to run on KyroOS.

## Application Structure

A typical KyroOS userspace application will have the following basic structure:

```
my_app/
├── main.c
├── Makefile
└── ... (other source files)
```

## KyroOS APIs

Applications interact with the KyroOS kernel primarily through:

*   **System Calls:** These are the primary interface for requesting services from the kernel (e.g., file I/O, process management, memory allocation). Refer to `src/include/syscall.h` for a complete list of available system calls and their arguments. You'll typically use a wrapper function that triggers the `int 0x80` (or equivalent) instruction.
*   **Userspace Library (`userspace/lib/`):** KyroOS provides a custom C standard library in `userspace/lib/`. This library offers common functions like string manipulation (`kstrcpy`, `kstrlen`), memory allocation (`kmalloc`, `kfree`), and basic I/O. Familiarize yourself with the functions available here before implementing your own.

## "Hello, World!" Example

Let's create a simple "Hello, World!" application:

**`main.c`:**
```c
#include <userspace.h> // Common userspace includes
#include <syscall.h>   // For KyroOS system calls
#include <kstring.h>   // For kprintf and other string functions

int main(int argc, char *argv[]) {
    kprintf("Hello, KyroOS World!\n");
    return 0;
}
```

**`Makefile`:**
```makefile
CC = x86_64-elf-gcc
LD = x86_64-elf-ld

TARGET = hello
SOURCES = main.c
OBJS = $(SOURCES:.c=.o)
LDFLAGS = -T ../../linker.ld -lc -lkuserspace

INCLUDES = -I../../src/include -I../../userspace/lib

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

.c.o:
	$(CC) -c $< -o $@ $(INCLUDES) -ffreestanding -O2 -Wall -Wextra

clean:
	rm -f $(OBJS) $(TARGET)
```

**Note:** The `LDFLAGS` might need adjustment based on the exact path to `linker.ld` and `userspace/lib` within your project structure. The `-lkuserspace` flag assumes your userspace library is compiled and available for linking.

## Building Your Application

1.  **Toolchain:** Ensure you have the `x86_64-elf-gcc` cross-compiler and associated tools installed.
    ```bash
    sudo apt-get install build-essential nasm xorriso gcc-x86-64-linux-gnu
    ```
2.  **Navigate:** Go to your application's directory (e.g., `my_app/`).
3.  **Build:** Run `make`. This will compile `main.c` and link it against the KyroOS userspace library and system call wrappers, producing an executable (e.g., `hello`).

## Integrating into KyroOS

To run your application on KyroOS, you will typically place the compiled executable into the `userspace/` directory within the KyroOS source tree. The KyroOS build process will then include it in the final ISO.

For more advanced examples and to understand how system utilities are built, explore the existing applications in the [KyroOS repository's `userspace/` directory](https://github.com/kyrosystems/KyroOS/tree/main/userspace). 
The core system components like `init` and `shell` are excellent resources for learning how to interact with the KyroOS kernel and its libraries.
