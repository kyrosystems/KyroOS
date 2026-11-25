;
; KyroOS x86_64 Long Mode Entry
; This file contains the 64-bit entry point after the 32-bit bootloader has enabled long mode.
; GDT and Paging setup is now done in C.
;
default rel

global long_mode_entry_point ; External entry point called from boot.asm
extern kmain_x64

section .text
bits 64

long_mode_entry_point:
    ; Reload segment registers for 64-bit mode.
    mov ax, 0x10    ; Kernel Data Selector (from GDT)
    mov ss, ax
    mov ds, ax
    mov es, ax
    xor rax, rax    ; Clear RAX
    mov fs, rax     ; Clear FS
    mov gs, rax     ; Clear GS

    ; Set up 64-bit stack
    mov rsp, kernel_stack_top

    ; Call C kernel entry point
    ; Arguments are passed in RDI, RSI, RDX for x86_64 SysV ABI
    ; EAX -> RDI (multiboot magic)
    ; EBX -> RSI (multiboot info address)
    ; 0 -> RDX (pml4 physical address - C will create its own)
    mov rdx, 0   ; No PML4 address from boot, C will make one
    mov rsi, rbx ; Move Multiboot info address from RBX to RSI
    mov rdi, rax ; Move Multiboot magic from RAX to RDI
    call kmain_x64

hang_loop:
    cli
    hlt
    jmp hang_loop

section .bss
align 4096
kernel_stack: resb 8192
kernel_stack_top:
