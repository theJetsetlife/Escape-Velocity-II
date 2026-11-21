/* Flat, position-independent blob. Everything (including .bss) is file-backed,
 * because WebKitty only maps the bytes it downloaded. */
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(8);
        __blob_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame*) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) }
}
