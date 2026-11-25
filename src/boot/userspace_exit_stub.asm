; src/boot/userspace_exit_stub.asm
; This stub is executed after thread_switch returns for the first time
; into a userspace thread.

global userspace_exit_stub
global userspace_trampoline
global userspace_thread_starter
global userspace_fork_entry

section .text


userspace_thread_starter:
    iretq

userspace_exit_stub:
    iretq


userspace_fork_entry:
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

userspace_trampoline:
    pop rdi         ; argc
    pop rsi         ; argv
    pop rax         ; entry_point
    jmp rax