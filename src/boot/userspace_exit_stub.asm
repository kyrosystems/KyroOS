; src/boot/userspace_exit_stub.asm
; This stub is executed after thread_switch returns for the first time
; into a userspace thread.

global userspace_exit_stub
global userspace_trampoline
global userspace_thread_starter

section .text


userspace_thread_starter:
    iretq

userspace_exit_stub:
    iretq


userspace_trampoline:
    pop rdi         ; argc
    pop rsi         ; argv
    pop rax         ; entry_point
    jmp rax