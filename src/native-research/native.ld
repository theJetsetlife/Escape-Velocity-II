/* Flat position-independent payload. .bss is file-backed because the loader
 * only copies raw .bin bytes. No dynamic linker or relocator is available. */
ENTRY(_start)
SECTIONS {
    . = 0;
    .text : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(8);
    }
    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame*)
                  *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash)
                  *(.got) *(.got.*) }
}
