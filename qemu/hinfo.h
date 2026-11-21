/* hinfo.h - facts the 32-bit multiboot stub (stub.c) collects for the 64-bit harness (h64.c).
 * Fixed physical address, packed, fixed-width types only (built -m32 and x86_64). */
#ifndef HINFO_H
#define HINFO_H

#define HINFO_ADDR   0x6000u
#define HINFO_MAGIC  0x4f464e4948425044ull   /* "DPBHINFO" */
#define HINFO_E820   32

struct hinfo_e820 { unsigned long long base, len; unsigned int type, pad; } __attribute__((packed));

struct hinfo {
	unsigned long long magic;
	unsigned long long upl_size;      /* bytes of upl-ps4.elf QEMU loaded at ELF_ADDR */
	unsigned long long tsc_hz;        /* host TSC frequency (cmdline) */
	unsigned long long fb;            /* std-vga linear framebuffer (BAR0) */
	unsigned int fb_w, fb_h, fb_pitch, rsvd;
	unsigned long long rsdp;          /* SeaBIOS RSDP from the 0xE0000 scan */
	unsigned long long mem_lo, mem_hi;/* bus-0 32-bit MMIO BAR span (PCI root-bridge aperture) */
	unsigned int n_e820, pad;
	struct hinfo_e820 e820[HINFO_E820];
} __attribute__((packed));

#define ELF_ADDR     0x00800000ull   /* = arm4 elf_pa on the PS4 (run 06:08) */
#define REGION_ADDR  0x12e00000ull   /* = arm5 region_pa on the PS4 (run 06:08) */
#define REGION_SIZE  0x00100000ull   /* 1 MiB, as the stage (FVs stay in the ELF copy) */
#define BOOT_ADDR    0x34d70000ull   /* = arm6 boot block on the PS4 (reserved like the stage does) */

#endif
