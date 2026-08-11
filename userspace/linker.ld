ENTRY(_start)

SECTIONS
{
    . = 0x400000;  /* Moved away from NULL page. Rebuild all userspace apps after changing this. */

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
        *(.bss)
    }
}