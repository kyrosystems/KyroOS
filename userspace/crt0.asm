bits 64
extern main
global _start

section .text
_start:
    ; thread.c stores argc at [rsp] and argv pointer at [rsp+16].
    mov rdi, [rsp]
    mov rsi, [rsp+16]
    and rsp, -16
    call main

    mov rdi, rax
    mov rax, 0
    int 0x80

.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
