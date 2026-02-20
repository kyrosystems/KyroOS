ENTRY(_start)

SECTIONS
{
    . = 0x0; /* Userspace programs typically loaded at a fixed low address */

    .text :
    {
        *(.text)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
    }

    .rodata :
    {
        *(.rodata)
    }
}
