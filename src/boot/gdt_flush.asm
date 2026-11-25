section .text

global gdt_flush
gdt_flush:
    ; rdi holds the address of the GDT pointer
    lgdt [rdi]
    
    ; DEBUG: Return immediately to see if lgdt works
    ; ret

    ; Reload data segments
    mov ax, 0x10    ; Kernel Data Selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    
    ; Reload code segment with a far jump
    push 0x08       ; Kernel Code Selector
    lea rax, [rel next_instr]
    push rax
    retfq
next_instr:
    ret

global tss_flush
tss_flush:
    mov ax, 0x28    ; TSS Selector
    ltr ax
    ret