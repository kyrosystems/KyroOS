/* src/tools/kpm/link.ld */
ENTRY(_start)

PHDRS {
    text PT_LOAD;
    data PT_LOAD;
}

SECTIONS {
    . = 0x400000; /* Standard userspace program base address */

    .text : {
        *(.text .text.*)
    } :text

    .data : {
        *(.data .data.*)
        *(.rodata .rodata.*)
    } :data

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        _bss_end = .;
    } :data
}
