// First userspace program for KyroOS

// A minimal syscall function
void syscall(long number, long arg1, long arg2, long arg3) {
    __asm__ volatile (
        "mov %0, %%rax\n\t"
        "mov %1, %%rdi\n\t"
        "mov %2, %%rsi\n\t"
        "mov %3, %%rdx\n\t"
        "int $0x80"
        :
        : "g"(number), "g"(arg1), "g"(arg2), "g"(arg3)
        : "rax", "rdi", "rsi", "rdx"
    );
}

void main() {
    const char* msg = "Hello from userspace!";
    
    // Call SYS_WRITE
    syscall(1, 1, (long)msg, 20);
    
    // Call SYS_EXIT
    syscall(0, 0, 0, 0);
    
    // Should never get here
    for(;;);
}
