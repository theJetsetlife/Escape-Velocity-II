/* crt0.s - PS4 Payload SDK entry convention (libPS4/crt0.s: `_start: jmp _main`) plus a
 * patchable config block at blob offset 0x10 (logging/probe_recv.py patch). Placed first by
 * linker.x (*(.text.start)). Defaults come from the Makefile. */
.intel_syntax noprefix
.section .text.start, "ax"
.global _start
_start:
    jmp _main
    .balign 16
.global prb_cfg
prb_cfg:
    .ascii "PRBCFG01"
    .byte PC_IP0, PC_IP1, PC_IP2, PC_IP3
    .byte (PC_PORT >> 8) & 0xff, PC_PORT & 0xff
    .byte PRB_ENV, PRB_LEVEL, PRB_STRICT, 0
    .short PRB_FW_HINT, 0
