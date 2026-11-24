/* Native PS4 9.00 WebKitty Bin Loader entry. No UEFI, no library dependency.
 * The loader calls address 0 with no arguments.
 */
#ifndef PC_IP0
#error "use make PC_IP=... PC_PORT=..."
#endif
    .section .text.start, "ax"
    .globl _start
_start:
    jmp     1f

    .balign 16
    .globl  scaffold_cfg
scaffold_cfg:
    .ascii  "DBDIAGCF"
    .byte   PC_IP0, PC_IP1, PC_IP2, PC_IP3
    .byte   (PC_PORT >> 8) & 0xff, PC_PORT & 0xff
    .short  0

1:
    push    %rbp
    mov     %rsp, %rbp
    push    %rbx
    push    %r12
    push    %r13
    push    %r14
    push    %r15
    and     $-16, %rsp
    call    scaffold_main
    lea     -40(%rbp), %rsp
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %rbx
    pop     %rbp
    ret

    .section .note.GNU-stack, "", @progbits
