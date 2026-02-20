section .text
global thread_switch
global userspace_trampoline
global thread_starter

extern kernel_hhdm_offset
extern thread_entry

; void thread_switch(thread_t* old_thread, thread_t* new_thread);
; rdi = old_thread
; rsi = new_thread
thread_switch:
    ; Save old thread's context
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Save old stack pointer
    mov [rdi + 32], rsp ; old_thread->rsp = rsp

    ; Check if we need to switch address space
    mov rax, [rdi + 40] ; rax = old_thread->pml4
    mov rbx, [rsi + 40] ; rbx = new_thread->pml4
    cmp rax, rbx
    je .no_cr3_switch

    ; Switch CR3
    mov rcx, [rel kernel_hhdm_offset]
    sub rbx, rcx ; rbx is now physical address of new_pml4
    mov cr3, rbx

.no_cr3_switch:
    ; Restore new thread's context
    mov rsp, [rsi + 32] ; rsp = new_thread->rsp
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    
    ret
    
; thread_starter(func, arg)
; This is the initial entry point for new threads.
; It expects func and arg to be on the stack.
thread_starter:
    pop rdi ; func
    pop rsi ; arg
    call thread_entry
    ; Should never return
    jmp $

section .note.GNU-stack noalloc noexec nowrite progbits
