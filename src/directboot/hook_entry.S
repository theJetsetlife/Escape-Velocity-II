/* Entry for the icc_query_nowait hook (runs from the kernel copy of the blob).
 *
 * icc_query_nowait has 4 callers on 9.00 (+0x3faf62 in the shutdown_final handler and three
 * ordinary ICC users). HW attempt 19:17 UTC: the unconditional hook fired during normal
 * operation and took over under a live OrbisOS. So: until db_reboot sets db_takeover, every
 * call passes straight through to the original function via db_tramp (registers untouched).
 *
 * db_tramp = the displaced original bytes (whole instructions, no RIP-relative operands, checked by
 * fwsel.c; on 9.00: push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; cmpb $4,1(%rdi) =
 * 15 bytes) + jmp qword [rip] -> icc + that length. db_arm fills it before copying the blob.
 */
    .section .text, "ax"
    .globl  db_hook_entry
db_hook_entry:
    cmpl    $0, db_takeover(%rip)
    jne     1f
    jmp     db_tramp
1:
    jmp     db_hook

    .section .data, "aw"
    .balign 16
    .globl  db_tramp
db_tramp:
    .fill   64, 1, 0xcc
    .globl  db_takeover
    .balign 4
db_takeover:
    .long   0

    .section .note.GNU-stack, "", @progbits
