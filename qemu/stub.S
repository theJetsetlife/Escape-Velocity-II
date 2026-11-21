/*
 * QEMU harness stub for the directboot handoff (rebuild/qemu). Loaded by QEMU as a multiboot
 * kernel (32-bit protected mode). stub_main() fills struct hinfo (hinfo.h) at 0x6000, then
 * we enter long mode with identity paging and jump to the 64-bit harness (h64.c) at
 * 0x01000000 with interrupts off, CS = 0x10, like the stage before upl_load.
 */
	.section .multiboot, "a"
	.align	4
	.long	0x1BADB002, 0x00000003, -(0x1BADB002 + 0x00000003)

	.text
	.code32
	.globl	_start
_start:
	cli
	movl	$stack_top, %esp
	pushl	%ebx
	call	stub_main
	movl	%cr4, %eax
	orl	$0x620, %eax			/* PAE | OSFXSR | OSXMMEXCPT: SSE on, as OrbisOS leaves it on the PS4 */
	movl	%eax, %cr4
	movl	$pml4, %eax
	movl	%eax, %cr3
	movl	$0xC0000080, %ecx
	rdmsr
	orl	$0x100, %eax			/* EFER.LME */
	wrmsr
	lgdt	gdt_desc
	lidt	idt_null			/* IDTR = {0, 0} exactly as hook.c switch_tables: EDK II copies the OLD IDT
					 * (DxeException.c InitializeCpuInterruptHandlers) and keeps its IST bits;
					 * SeaBIOS IVT bytes there gave the timer gate IST3 -> stack 0 -> #PF */
	movl	%cr0, %eax
	andl	$~0x0000000c, %eax		/* EM=0, TS=0 (the stage does clts too) */
	orl	$0x80000003, %eax		/* PG | MP | PE */
	movl	%eax, %cr0
	ljmp	$0x10, $long_entry
	.code64
long_entry:
	movl	$0x18, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs
	movq	$0x01000000, %rax
	jmp	*%rax

	.data
	.align	16
gdt:
	.quad	0
	.quad	0x00cf9a000000ffff		/* 0x08: 32-bit code */
	.quad	0x00af9a000000ffff		/* 0x10: 64-bit code (kexec's selector) */
	.quad	0x00cf92000000ffff		/* 0x18: data */
gdt_desc:
	.word	. - gdt - 1
	.long	gdt
idt_null:
	.word	0
	.long	0

	.bss
	.align	4096
	.globl	pml4, pdpt
pml4:	.space	4096
pdpt:	.space	4096
	.space	16384
stack_top:
