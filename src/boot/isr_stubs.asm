;
; KyroOS Interrupt Service Routine (ISR) Stubs
;
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7, irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
global isr128 ; Syscall interrupt

extern isr_handler
extern syscall_handler

%macro PUSH_REGS 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro POP_REGS 0
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
%endmacro

; Macro for ISRs that don't push an error code
%macro ISR_NO_ERR 1
    global isr%1
    isr%1:
        cli
        push 0  ; Dummy error code
        push %1 ; Interrupt number
        jmp isr_common_stub
%endmacro

; Macro for ISRs that do push an error code
%macro ISR_ERR 1
    global isr%1
    isr%1:
        cli
        ; Error code is already on the stack
        push %1 ; Interrupt number
        jmp isr_common_stub
%endmacro

; Macro for IRQs
%macro IRQ 2
    global irq%1
    irq%1:
        cli
        push 0  ; Dummy error code
        push %2 ; Interrupt number
        jmp isr_common_stub
%endmacro

section .text

; This is the common stub for all ISRs (exceptions and IRQs)
isr_common_stub:
    PUSH_REGS
    mov rdi, rsp ; Pass pointer to struct registers to C handler
    call isr_handler
    POP_REGS
    add rsp, 16 ; Pop int_no and err_code
    iretq

; Syscall handler stub (int 0x80)
global isr128
isr128:
    cli
    ; No error code pushed by 'int' instruction
    push 0  ; Dummy error code
    push 128; Interrupt number (for consistency, though not strictly needed)
    PUSH_REGS
    mov rdi, rsp ; Pass pointer to struct registers to C handler
    call syscall_handler
    POP_REGS
    add rsp, 16 ; Pop int_no and err_code
    sti
    iretq

; Define all ISRs
ISR_NO_ERR 0
ISR_NO_ERR 1
ISR_NO_ERR 2
ISR_NO_ERR 3
ISR_NO_ERR 4
ISR_NO_ERR 5
ISR_NO_ERR 6
ISR_NO_ERR 7
ISR_ERR 8
ISR_NO_ERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NO_ERR 15
ISR_NO_ERR 16
ISR_ERR 17
ISR_NO_ERR 18
ISR_NO_ERR 19
ISR_NO_ERR 20
ISR_ERR 21
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_NO_ERR 29
ISR_NO_ERR 30
ISR_NO_ERR 31

; Define all IRQs
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

section .note.GNU-stack noalloc noexec nowrite progbits