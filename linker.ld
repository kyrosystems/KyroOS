OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VIRTUAL_BASE = 0xFFFFFFFF80000000;

PHDRS
{
    text PT_LOAD FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    rodata PT_LOAD FLAGS((1 << 2)) ; /* Read only */
    data PT_LOAD FLAGS((1 << 1) | (1 << 2)) ; /* Write + Read */
}

SECTIONS {
    . = KERNEL_VIRTUAL_BASE;
    _kernel_start = .;

    .text : {
        *(.text*)
    } :text

    . = ALIGN(4096);

    .rodata : {
        *(.rodata*)
    } :rodata

    . = ALIGN(4096);

    .data : {
        . = ALIGN(8);
        KEEP(*(.limine_reqs_start))
        KEEP(*(.limine_reqs))
        KEEP(*(.limine_reqs_end))
        *(.data*)
    } :data

    . = ALIGN(4096);

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data
    
    . = ALIGN(4096);

    _kernel_end = .;
    _boot_end = .;

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.comment)
        *(.eh_frame)
    }
}
