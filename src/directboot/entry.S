/* Blob entry: offset 0 is executed directly by WebKitty's runPayload (call_void).
 * A fixed config block at offset 0x10 holds the PC address; tools/diag_recv.py can
 * patch it in the .bin without rebuilding ("DBBOOTCF" + ip[4] + port[2], network order). */
#ifndef PC_IP0
#error "build through the Makefile (PC_IP / PC_PORT)"
#endif

    .section .text.start, "ax"
    .globl _start
_start:
    jmp     1f

    .balign 16
    .globl  db_cfg
db_cfg:
    .ascii  "DBBOOTCF"
    .byte   PC_IP0, PC_IP1, PC_IP2, PC_IP3
    .byte   (PC_PORT >> 8) & 0xff, PC_PORT & 0xff
    .short  0

1:
    push    %rbp                 /* stack alignment from the ROP caller is unknown */
    mov     %rsp, %rbp
    push    %rbx                 /* keep every callee-saved register for the caller */
    push    %r12
    push    %r13
    push    %r14
    push    %r15
    and     $-16, %rsp
    call    db_main
    lea     -40(%rbp), %rsp
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %rbx
    pop     %rbp
    ret

    .section .note.GNU-stack, "", @progbits
