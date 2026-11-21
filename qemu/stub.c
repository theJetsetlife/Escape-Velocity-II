/* QEMU harness stub, C part (32-bit, freestanding). Collects the machine facts the PS4 stage
 * would have (framebuffer, RSDP, RAM map, PCI MMIO span) into struct hinfo at HINFO_ADDR and
 * builds identity page tables; stub.S then enters long mode and jumps to h64 at 0x01000000. */
#include <stdint.h>
#include "hinfo.h"

extern uint64_t pml4[512], pdpt[512];

static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static void dbg(const char *s) { while (*s) outb(0xE9, (uint8_t)*s++); }

static uint32_t pci_rd(uint32_t dev, uint32_t fn, uint32_t off)
{
	outl(0xCF8, 0x80000000u | (dev << 11) | (fn << 8) | (off & 0xFC));
	return inl(0xCFC);
}

struct mbi { uint32_t flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr, syms[4], mmap_length, mmap_addr; };
struct mmap_ent { uint32_t size; uint64_t base, len; uint32_t type; } __attribute__((packed));

static uint64_t arg(const char *cmd, const char *key)
{
	for (; *cmd; cmd++) {
		const char *a = cmd, *k = key;
		while (*k && *a == *k) { a++; k++; }
		if (!*k && *a == '=') {
			uint64_t v = 0;
			for (a++; *a >= '0' && *a <= '9'; a++) v = v * 10 + (uint64_t)(*a - '0');
			return v;
		}
	}
	return 0;
}

void stub_main(struct mbi *mbi)
{
	volatile struct hinfo *h = (volatile struct hinfo *)HINFO_ADDR;
	const char *cmd = (mbi->flags & 4) ? (const char *)mbi->cmdline : "";
	dbg("stub: start\n");

	/* Bochs VBE: 1920x1080x32 linear, the geometry the PS4 stage hands over */
	outw(0x1CE, 4); outw(0x1CF, 0);
	outw(0x1CE, 1); outw(0x1CF, 1920);
	outw(0x1CE, 2); outw(0x1CF, 1080);
	outw(0x1CE, 3); outw(0x1CF, 32);
	outw(0x1CE, 4); outw(0x1CF, 0x41);

	uint32_t fb = 0, lo = 0xffffffffu, hi = 0;
	for (uint32_t d = 0; d < 32; d++) {
		if ((pci_rd(d, 0, 0) & 0xFFFF) == 0xFFFF) continue;
		if (!fb && (pci_rd(d, 0, 8) >> 24) == 0x03) fb = pci_rd(d, 0, 0x10) & ~0xFu;
		for (uint32_t f = 0; f < 8; f++) {
			if ((pci_rd(d, f, 0) & 0xFFFF) == 0xFFFF) continue;
			for (uint32_t b = 0x10; b <= 0x24; b += 4) {
				uint32_t v = pci_rd(d, f, b);
				if (!v || (v & 1)) continue;                 /* empty or I/O */
				uint32_t base = v & ~0xFu;
				if (base < 0x80000000u) continue;
				if (base < lo) lo = base;
				if (base > hi) hi = base;
				if ((v & 6) == 4) b += 4;                    /* 64-bit BAR: skip high dword */
			}
		}
	}

	uint32_t rsdp = 0;
	for (uint32_t a = 0xE0000; a < 0x100000 && !rsdp; a += 16)
		if (*(uint64_t *)a == 0x2052545020445352ull)        /* "RSD PTR " */
			rsdp = a;

	h->upl_size = arg(cmd, "upl_size");
	h->tsc_hz = arg(cmd, "tsc_hz");
	h->fb = fb; h->fb_w = 1920; h->fb_h = 1080; h->fb_pitch = 7680; h->rsvd = (uint32_t)arg(cmd, "dbflags");
	h->rsdp = rsdp;
	h->mem_lo = lo == 0xffffffffu ? 0xc0000000u : (lo & ~0xFFFFFu);
	h->mem_hi = 0xfebfffffu;                                 /* below IOAPIC/HPET/LAPIC */
	(void)hi;
	h->n_e820 = 0;
	if (mbi->flags & 0x40)
		for (uint32_t off = 0; off < mbi->mmap_length && h->n_e820 < HINFO_E820; ) {
			struct mmap_ent *e = (struct mmap_ent *)(mbi->mmap_addr + off);
			h->e820[h->n_e820].base = e->base; h->e820[h->n_e820].len = e->len;
			h->e820[h->n_e820].type = e->type; h->e820[h->n_e820].pad = 0;
			h->n_e820++;
			off += e->size + 4;
		}
	h->magic = HINFO_MAGIC;

	for (uint32_t i = 0; i < 512; i++)
		pdpt[i] = ((uint64_t)i << 30) | 0x83;                /* 1 GiB pages, identity */
	pml4[0] = (uint64_t)(uintptr_t)pdpt | 0x3;
	dbg("stub: long mode -> h64\n");
}
