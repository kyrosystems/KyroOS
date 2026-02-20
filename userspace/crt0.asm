; Userspace Entry Point Stub
bits 64
extern main
global _start

section .text
_start:
    ; The stack expects argc and argv to be passed in registers RDI and RSI
    ; which are preserved/set by the kernel's iretq frame or trampoline.
    
    call main
    
    ; Exit syscall (0 for KyroOS)
    mov rax, 0
    mov rdi, 0
    int 0x80
    
    ; Should not reach here
    hlt