/*
 * Minimal linker script for userspace programs
 */
ENTRY(_start)

SECTIONS
{
    . = 0x400000; /* Userspace programs start at 4MB */

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        _bss_start = .;
        *(.bss)
        _bss_end = .;
    }

    _end = .;
}
