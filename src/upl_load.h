/* upl_load.h - inputs for upl_load() (src/upl_load.c). */
#ifndef UPL_LOAD_H
#define UPL_LOAD_H
#include "ps4.h"
#include "upl_hob.h"

/* ---- inputs the surrounding handoff code fills from verified runtime facts ---- */
struct upl_memmap_entry { u64 base, length; u32 type; };   /* type: see E820_* in ps4.h caller */
#define UPL_MM_RAM      1
#define UPL_MM_RESERVED 2
#define UPL_MM_ACPI     3
#define UPL_MM_NVS      4
#define UPL_MM_MAX      128

struct upl_inputs {
	const u8 *elf; u64 elf_size;         /* the UPL ELF image bytes */
	u64 region_phys, region_size;        /* scratch region we own (>= a few MB) */
	/* framebuffer (ledger H1): geometry static, base runtime */
	u64 fb_base, fb_size; u32 fb_w, fb_h, fb_stride, fb_bpp;
	u64 rsdp;                            /* ACPI RSDP phys (0 = none) */
	const upl_pci_root_bridge *pci_rb;   /* PCI root bridge; 0 = PS4 Fat defaults (bus 0, BARs as OrbisOS assigned) */
	struct upl_memmap_entry mm[UPL_MM_MAX]; u32 mm_count;
	const ps4_directboot_info *ps4;      /* optional platform HOB payload (0 = none) */
	u64 relocs_applied;                  /* out: diagnostics */
	void (*progress)(void *ctx, u32 step); /* optional: 1 loaded, 2 relocated, 3 FVs, 4 HOBs/jump */
	void *progress_ctx;
	u64 test_entry, test_hobs, test_stack; /* out: UPL_HOST_TEST only */
};

/* Loads the UPL ELF into region_phys, builds the HOB list, jumps. Returns only on error. */
const char *upl_load(struct upl_inputs *in);

#endif
