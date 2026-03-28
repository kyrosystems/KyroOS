# Loadable Kernel Modules (LKM)

KyroOS supports Loadable Kernel Modules (LKM) - a mechanism for dynamically loading and unloading kernel code at runtime.

## Overview

The LKM system allows you to:
- Load device drivers without recompiling the kernel
- Unload unused drivers to save memory
- Verify module signatures for security
- Extend kernel functionality dynamically

## Module Format

KyroOS modules are ELF64 relocatable objects (`.kmod` files) with the following structure:

1. **ELF Header** - Standard ELF64 header with `ET_REL` type
2. **Sections**:
   - `.text` - Executable code
   - `.data` - Initialized data
   - `.rodata` - Read-only data
   - `.bss` - Uninitialized data
   - `.symtab` - Symbol table
   - `.strtab` - String table
   - `.rela.text` - Relocation entries for .text
   - `.rela.data` - Relocation entries for .data

3. **Required Symbols**:
   - `module_init` or `__lkm_init_ptr` - Module initialization function
   - `module_exit` or `__lkm_exit_ptr` - Module cleanup function
   - `module_name` (optional) - Human-readable module name

## Creating a Module

### Example Module Source

```c
// hello_module.c
#include <stdint.h>
#include <stddef.h>

const char *module_name = "hello_module";

int module_init(void) {
    // Initialization code here
    // klog(LOG_INFO, "Hello from module!");
    return 0; // Return 0 on success
}

void module_exit(void) {
    // Cleanup code here
    // klog(LOG_INFO, "Goodbye from module!");
}
```

### Building a Module

```bash
# Compile as position-independent ELF64 relocatable
x86_64-linux-gnu-gcc -c -ffreestanding -fPIC \
    -nostdlib -mcmodel=kernel \
    -I/path/to/kyroos/src/include \
    hello_module.c -o hello_module.o

# The .o file is your module (can be renamed to .kmod)
```

## Shell Commands

### lsmod - List Loaded Modules

```
> lsmod
Loaded modules:
----------------
  hello_module       @ 0xFFFF80001000  [1 refs]
  network_driver     @ 0xFFFF80002000  [2 refs]
```

### insmod - Insert Module

```
> insmod /bin/hello_module.kmod
Module 'hello_module' loaded successfully.
```

### rmmod - Remove Module

```
> rmmod hello_module
Module 'hello_module' unloaded successfully.
```

## Kernel API for Modules

Modules can use the following kernel functions:

### Memory Management
- `kmalloc(size_t size)` - Allocate kernel memory
- `kfree(void *ptr)` - Free kernel memory

### Logging
- `klog(int level, const char *fmt, ...)` - Kernel logging
- `klog_print_str(const char *str, bool flush)` - Print string
- `klog_putchar(char c)` - Print character

### String Functions
- `memcpy`, `memset`, `strlen`, `strcmp`, `strcpy`, `strncpy`

### VFS
- `vfs_read`, `vfs_write`, `vfs_open`, `vfs_close`
- `vfs_finddir`, `vfs_resolve_path`

### Threading
- `thread_create(void (*entry)(void*), void *arg)`
- `schedule()` - Yield CPU

### Synchronization
- `disable_interrupts()` / `enable_interrupts()`

## Security

### Module Signature

Modules can be signed with RSA+SHA256 for verification:

1. Hash the ELF content with SHA256
2. Sign the hash with private RSA key
3. Append signature and magic number to module

The kernel verifies signatures before loading unsigned modules may be rejected depending on security settings.

## Implementation Details

### Loading Process

1. **Validation**: Check ELF header and module format
2. **Memory Allocation**: Allocate executable memory for code
3. **Section Loading**: Copy .text, .data, .rodata sections
4. **Relocation**: Process .rela.* sections to fix up addresses
5. **Symbol Resolution**: Resolve kernel symbols
6. **Initialization**: Call module_init function

### Relocation Types Supported

- `R_X86_64_64` - 64-bit absolute relocation
- `R_X86_64_PC32` - 32-bit PC-relative relocation
- `R_X86_64_32` - 32-bit absolute relocation
- `R_X86_64_32S` - 32-bit signed absolute relocation

## Troubleshooting

### Module Fails to Load

1. Check kernel log for error messages
2. Verify ELF format: `readelf -h module.kmod`
3. Ensure all symbols are resolved
4. Check for architecture mismatches

### Common Errors

- **"Invalid ELF magic"** - Not a valid ELF file
- **"Not a 64-bit ELF"** - Wrong architecture
- **"Unresolved symbol"** - Missing kernel function
- **"Init failed"** - module_init returned error

## Future Enhancements

- Module dependency tracking
- Automatic module loading
- Module versioning
- Enhanced security with module blacklisting
