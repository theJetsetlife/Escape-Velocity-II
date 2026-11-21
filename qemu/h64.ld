/* Flat 64-bit image at 0x01000000; h64_main must be the first byte (stub.S jumps there). */
ENTRY(h64_main)
SECTIONS
{
    . = 0x01000000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
