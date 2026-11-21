/* probe.ld - the PS4 Payload SDK linker.x layout (entry first, text/rodata/data), with two changes:
 *  - .bss is folded into the file (zero-filled PROGBITS inside .data): objcopy -O binary drops a
 *    trailing NOBITS section, and the payload must not rely on the loader's buffer being larger
 *  - dynamic-linking leftovers of the static PIE link are discarded (the image has no dynamic relocs) */
OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0;
    .text : {
        *(.text.start)
        *(.text*)
    }
    .rodata : {
        *(.rodata)
        *(.rodata*)
    }
    .data : {
        *(.data)
        *(.data*)
        *(.bss)
        *(.bss*)
        *(COMMON)
        BYTE(0)
        . = ALIGN(16);
    }
    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame*) *(.interp)
        *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash) *(.dynamic) *(.got) *(.got.plt)
    }
}
