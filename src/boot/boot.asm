; KyroOS Limine Entry Point
; This file sets up the stack and passes control to the C kernel.
default rel
bits 64

section .bss
align 4096
stack:
    resb 16384
stack_top:

global pml4
align 4096
pml4:
    resb 4096

section .text
global _start
extern kmain_x64

_start:
    ; Hardware-level Serial Debug: Write '!' to COM1 immediately
    mov dx, 0x3F8
    mov al, '!'
    out dx, al

    ; No arguments are passed in registers for Limine protocol.
    ; All info is in the static request structures.
    
    cli ; Disable interrupts immediately

    ; Set up the stack.
    mov rsp, stack_top

    ; Call the C entry point.
    call kmain_x64

    ; If the kernel returns, hang.
hang:
    cli
    hlt
    jmp hang