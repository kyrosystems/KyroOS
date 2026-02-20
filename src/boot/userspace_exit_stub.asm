; src/boot/userspace_exit_stub.asm
; This stub is executed in kernel mode after a context switch
; It prepares the stack for an IRETQ to transition to userspace.

global userspace_exit_stub
global userspace_trampoline ; Make it globally accessible
global userspace_thread_starter

section .text
userspace_exit_stub:
    ; The stack currently contains the registers saved by thread_switch,
    ; followed by the IRETQ frame (SS, RSP, RFLAGS, CS, RIP) pushed by thread_create_userspace.

    ; We need to move the IRETQ frame from the kernel stack
    ; to the correct position for IRETQ.
    ; RSP should point directly to the saved SS value.

    ; Simply execute IRETQ.
    ; This will pop SS, RSP, RFLAGS, CS, RIP from the *current* kernel stack (RSP)
    ; and switch to userspace.

    iretq

userspace_thread_starter:
    ; This function is called by the `ret` in `thread_switch`.
    ; The stack pointer (RSP) now points to the IRETQ frame.
    ; Callee-saved registers were restored by thread_switch.
    ; argc is in RBX, argv is in R12.
    mov rdi, rbx
    mov rsi, r12
    iretq

userspace_trampoline:
    ; The userspace stack has:
    ; entry_point (main)
    ; argv_ptr
    ; argc

    pop rdi  ; argc to RDI
    pop rsi  ; argv to RSI
    pop rax  ; entry_point to RAX

    jmp rax  ; Jump to the userspace entry point
