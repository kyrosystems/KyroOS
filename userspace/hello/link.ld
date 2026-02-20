ENTRY(main)
SECTIONS
{
    . = 0x400000; /* Start at 4MB */

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
}