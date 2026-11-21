/*
 * upl_load.c — clean-room EDK2 Universal Payload loader + HOB builder.
 *
 * Runs in flat 64-bit long mode (interrupts off, one CPU) with identity-mapped
 * low physical memory. Given the UPL ELF image and a scratch region, it:
 *   1. loads the entry module's PT_LOAD segments            (ledger G3)
 *   2. applies its PT_DYNAMIC R_X86_64_RELATIVE relocations (ledger G3)
 *   3. places the .upld.uefi_fv / .upld.bds_fv firmware vols (ledger G3)
 *   4. builds the HOB list the entry consumes               (ledger G2)
 *   5. jumps to e_entry with RCX = HOB list (MS x64 ABI)    (ledger G1)
 * Returns an error string only on failure; on success it does not return.
 *
 * Nothing here trusts the old shim. Struct/ABI facts come from upl_hob.h
 * (primary edk2) and the verified ledger.
 */
#include "ps4.h"
#include "upl_hob.h"

/* ---- freestanding mem helpers ---- */
static void *u_memcpy(void *d, const void *s, u64 n){ u8 *p=d; const u8 *q=s; while(n--)*p++=*q++; return d; }
static void *u_memset(void *d, int c, u64 n){ u8 *p=d; while(n--)*p++=(u8)c; return d; }
static int   u_memeq(const void *a, const void *b, u64 n){ const u8 *x=a,*y=b; while(n--) if(*x++!=*y++) return 0; return 1; }
static u64   align_up(u64 v, u64 a){ return (v + a - 1) & ~(a - 1); }

/* ---- ELF64 (we define our own; glibc elf.h is not linked) ---- */
typedef struct { u8 ident[16]; u16 type, machine; u32 version; u64 entry, phoff, shoff;
	u32 flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } Elf64_Ehdr;
typedef struct { u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align; } Elf64_Phdr;
typedef struct { u32 name, type; u64 flags, addr, offset, size; u32 link, info; u64 addralign, entsize; } Elf64_Shdr;
typedef struct { s64 tag; u64 val; } Elf64_Dyn;
typedef struct { u64 offset, info, addend; } Elf64_Rela;
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define DT_NULL      0
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_RELAENT   9
#define R_X86_64_RELATIVE 8

#include "upl_load.h"

/* ---- HOB builder cursor ---- */
struct hobgen { u8 *base; u64 cur, cap; };
static void *hob_add(struct hobgen *g, u16 type, u64 len){
	len = align_up(len, 8);
	if (g->cur + len > g->cap) return (void*)0;
	hob_header *h = (hob_header*)(g->base + g->cur);
	u_memset(h, 0, len);
	h->type = type; h->length = (u16)len;
	g->cur += len;
	return h;
}
static void *hob_add_guid(struct hobgen *g, efi_guid id, u64 data_len){
	hob_guid_type *h = hob_add(g, HOB_TYPE_GUID_EXTENSION, sizeof(*h) + data_len);
	if (h) h->name = id;
	return h ? (void*)(h + 1) : (void*)0;   /* pointer to the data area */
}
static void hob_resource(struct hobgen *g, u32 rtype, u32 attr, u64 start, u64 len){
	hob_resource_descriptor *r = hob_add(g, HOB_TYPE_RESOURCE_DESCRIPTOR, sizeof(*r));
	if (!r) return;
	r->resource_type = rtype; r->resource_attribute = attr;
	r->physical_start = start; r->resource_length = len;
}

/* MS x64 ABI jump (ledger G1): RCX = HobList, 32B shadow, 16-byte aligned stack, never returns. */
static __attribute__((noreturn)) void jump_to_upl(u64 entry, u64 hob_list, u64 stack_top){
	__asm__ volatile(
		"mov %2, %%rsp\n\t"
		"and $-16, %%rsp\n\t"
		"sub $32, %%rsp\n\t"       /* shadow space */
		"mov %1, %%rcx\n\t"        /* arg0 = HobList */
		"call *%0\n\t"
		"1: hlt\n\t jmp 1b\n\t"
		: : "r"(entry), "r"(hob_list), "r"(stack_top) : "rcx", "memory");
	__builtin_unreachable();
}

#define RES_SYSMEM_ATTR ( \
	EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED | \
	EFI_RESOURCE_ATTRIBUTE_TESTED  | EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE)

const char *upl_load(struct upl_inputs *in)
{
	if (in->elf_size < sizeof(Elf64_Ehdr)) return "upl: image too small";
	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)in->elf;
	static const u8 elf_magic[4] = { 0x7f, 'E', 'L', 'F' };
	if (!u_memeq(eh->ident, elf_magic, 4)) return "upl: not an ELF";
	if (eh->ident[4] != 2 /*ELFCLASS64*/ || eh->machine != 62 /*x86-64*/) return "upl: not ELF64/x86-64";

	/* Load the entry module at bias = region_phys (link base is 0). */
	const u64 bias = in->region_phys;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)(in->elf + eh->phoff);
	u64 image_end = 0;
	const Elf64_Phdr *dyn_ph = (void*)0;

	/* ---- step 1: load PT_LOAD segments ---- */
	for (u16 i = 0; i < eh->phnum; i++) {
		const Elf64_Phdr *p = &ph[i];
		if (p->type == PT_DYNAMIC) { dyn_ph = p; continue; }
		if (p->type != PT_LOAD) continue;
		if (p->offset + p->filesz > in->elf_size) return "upl: PT_LOAD past EOF";
		u8 *dst = (u8*)(bias + p->vaddr);
		u_memcpy(dst, in->elf + p->offset, p->filesz);
		if (p->memsz > p->filesz) u_memset(dst + p->filesz, 0, p->memsz - p->filesz);  /* .bss */
		u64 e = p->vaddr + p->memsz;
		if (e > image_end) image_end = e;
	}
	if (!dyn_ph) return "upl: no PT_DYNAMIC";
	if (in->progress) in->progress(in->progress_ctx, 1);

	/* ---- step 2: apply R_X86_64_RELATIVE ----
	 * Same method as edk2 UefiPayloadPkg/PayloadLoaderPeim/ElfLib/Elf64Lib.c RelocateElf64Dynamic:
	 * DT_RELA/DT_RELASZ name a section (match sh_addr + sh_size) and the entries are read from the
	 * FILE. In the built upl-ps4.elf DT_RELA == 0 and .rela is not in any PT_LOAD, so reading the
	 * table through the loaded image (or treating 0 as "none") would silently skip every fixup. */
	{
		/* .dynamic is read from the FILE via its section header (as edk2 does): in the CLANGDWARF
		 * UPL the section's file offset is not where PT_LOAD maps its vaddr (packed after .data),
		 * so the copy at bias + vaddr is NOT the dynamic table. */
		const Elf64_Shdr *shd = (const Elf64_Shdr *)(in->elf + eh->shoff);
		const Elf64_Dyn *d = (void*)0, *dend = (void*)0;
		for (u16 i = 0; i < eh->shnum; i++)
			if (shd[i].type == 6 /*SHT_DYNAMIC*/ && shd[i].offset + shd[i].size <= in->elf_size) {
				d = (const Elf64_Dyn *)(in->elf + shd[i].offset);
				dend = (const Elf64_Dyn *)(in->elf + shd[i].offset + shd[i].size);
				break;
			}
		if (!d) return "upl: no SHT_DYNAMIC section";
		u64 rela = 0, relasz = 0, relaent = sizeof(Elf64_Rela); int have_rela = 0;
		for (; d < dend && d->tag != DT_NULL; d++) {
			if (d->tag == DT_RELA)         { rela = d->val; have_rela = 1; }
			else if (d->tag == DT_RELASZ)  relasz  = d->val;
			else if (d->tag == DT_RELAENT) relaent = d->val;
		}
		if (have_rela && relasz) {
			const Elf64_Shdr *sh = (const Elf64_Shdr *)(in->elf + eh->shoff);
			const Elf64_Shdr *rs = (void*)0;
			for (u16 i = 0; i < eh->shnum; i++)
				if (sh[i].type == 4 /*SHT_RELA*/ && sh[i].addr == rela && sh[i].size == relasz) { rs = &sh[i]; break; }
			if (!rs) return "upl: DT_RELA section not found";
			if (rs->offset + rs->size > in->elf_size || relaent < sizeof(Elf64_Rela)) return "upl: bad .rela";
			for (u64 off = 0; off < relasz; off += relaent) {
				const Elf64_Rela *r = (const Elf64_Rela *)(in->elf + rs->offset + off);
				if ((r->info & 0xffffffffu) == R_X86_64_RELATIVE) {
					if (r->offset + 8 > image_end) return "upl: reloc outside image";
					*(u64*)(bias + r->offset) = bias + r->addend;
				}
			}
			in->relocs_applied = relasz / relaent;
		}
	}

	if (in->progress) in->progress(in->progress_ctx, 2);

	/* ---- step 3: firmware volumes IN PLACE in the ELF copy ----
	 * HW run 05:56 UTC: OrbisOS had no free 8.1 MiB contiguous block for a region that also held
	 * a second copy of the FVs (40 no-wait tries). The ELF copy is already contiguous, page
	 * aligned and reserved in the memory map; both FV headers declare 16-byte alignment
	 * (attr 0x4feff, no WEAK_ALIGNMENT), which is exactly what DxeCore FwVolBlock.c checks, and
	 * their file offsets (0x11760, 0x3ba760) are 16-byte aligned. So the region only needs the
	 * entry image + HOBs + stack. */
	const Elf64_Shdr *sh = (const Elf64_Shdr *)(in->elf + eh->shoff);
	const char *shstr = (const char *)(in->elf + sh[eh->shstrndx].offset);
	u64 place = align_up(bias + image_end, 0x10000);   /* HOBs follow the entry module */
	u64 fv_uefi = 0, fv_uefi_sz = 0, fv_bds = 0, fv_bds_sz = 0;
	for (u16 i = 0; i < eh->shnum; i++) {
		const char *nm = shstr + sh[i].name;
		int is_uefi = u_memeq(nm, ".upld.uefi_fv", 14);
		int is_bds  = u_memeq(nm, ".upld.bds_fv", 13);
		if (!is_uefi && !is_bds) continue;
		if (sh[i].offset + sh[i].size > in->elf_size) return "upl: FV section past EOF";
		u64 fv = (u64)(in->elf + sh[i].offset);
		if (fv & 0xf) return "upl: FV not 16-byte aligned in the ELF copy";
		if (!u_memeq((const u8 *)fv + 0x28, "_FVH", 4)) return "upl: FV section has no _FVH";
		if (is_uefi) { fv_uefi = fv; fv_uefi_sz = sh[i].size; }
		else         { fv_bds  = fv; fv_bds_sz  = sh[i].size; }
	}
	if (!fv_uefi) return "upl: .upld.uefi_fv not found";
	if (in->progress) in->progress(in->progress_ctx, 3);

	/* ---- step 4: build the HOB list (ledger G2) ---- */
	u64 hob_area  = align_up(place, 0x1000);
	u64 hob_cap   = 0x20000;                                   /* 128K of HOBs is ample */
	u64 stack_top = in->region_phys + in->region_size;         /* top of our region */
	if (stack_top <= hob_area + hob_cap) return "upl: region too small for HOBs/stack";
	struct hobgen g = { .base = (u8*)hob_area, .cur = 0, .cap = hob_cap };

	/* PHIT first. memory_* describe the HOB producer window; free window follows the HOBs. */
	hob_handoff_info *phit = hob_add(&g, HOB_TYPE_HANDOFF, sizeof(*phit));
	if (!phit) return "upl: PHIT alloc";
	phit->version        = EFI_HOB_HANDOFF_TABLE_VERSION;
	phit->boot_mode      = 0;                                   /* BOOT_WITH_FULL_CONFIGURATION */
	phit->memory_bottom  = in->region_phys;
	phit->memory_top     = stack_top;
	phit->free_memory_bottom = hob_area;                        /* fixed up to end_of_hob_list below */
	phit->free_memory_top    = stack_top;

	/* CPU HOB: physical address width from CPUID 0x80000008 (40 on the Fat, run 2), 16 I/O bits
	 * (what edk2's non-UPL UefiPayloadEntry passes to BuildCpuHob). */
	{
		hob_cpu *ch = hob_add(&g, HOB_TYPE_CPU, sizeof(*ch));
		if (!ch) return "upl: CPU HOB alloc";
		u32 a = 0x80000008, b, c2 = 0, d;
		__asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c2), "=d"(d));
		ch->size_of_memory_space = (u8)((a & 0xff) ? (a & 0xff) : 36);
		ch->size_of_io_space = 16;
	}

	/* resource descriptors = the memory map (verified at runtime, passed in). */
	for (u32 i = 0; i < in->mm_count && i < UPL_MM_MAX; i++) {
		struct upl_memmap_entry *m = &in->mm[i];
		if (m->type == UPL_MM_RAM)
			hob_resource(&g, EFI_RESOURCE_SYSTEM_MEMORY, RES_SYSMEM_ATTR, m->base, m->length);
		else
			hob_resource(&g, EFI_RESOURCE_MEMORY_RESERVED,
			             EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED,
			             m->base, m->length);
	}

	/* REQUIRED: gUniversalPayloadExtraDataGuid; Entry[0].identifier = "uefi_fv" (DXE FV). */
	{
		u32 count = fv_bds ? 2 : 1;
		u64 dlen = sizeof(upl_generic_header) + 4 /*count*/ + count * sizeof(upl_extra_data_entry);
		efi_guid gid = (efi_guid)GUID_UPL_EXTRA_DATA;
		upl_extra_data *xd = hob_add_guid(&g, gid, dlen);
		if (!xd) return "upl: ExtraData HOB alloc";
		xd->header.revision = 1; xd->header.length = (u16)dlen;
		xd->count = count;
		u_memset(xd->entry[0].identifier, 0, 16);
		u_memcpy(xd->entry[0].identifier, "uefi_fv", 7);
		xd->entry[0].base = fv_uefi; xd->entry[0].size = fv_uefi_sz;
		if (fv_bds) {
			u_memset(xd->entry[1].identifier, 0, 16);
			u_memcpy(xd->entry[1].identifier, "bds_fv", 6);
			xd->entry[1].base = fv_bds; xd->entry[1].size = fv_bds_sz;
		}
	}

	/* optional: ACPI RSDP */
	if (in->rsdp) {
		efi_guid gid = (efi_guid)GUID_UPL_ACPI_TABLE;
		upl_acpi_table *at = hob_add_guid(&g, gid, sizeof(upl_generic_header) + sizeof(efi_phys));
		if (at) { at->header.revision = 1; at->header.length = sizeof(*at); at->rsdp = in->rsdp; }
	}


	/* PCI root bridge (restored; lost in the rollback to 7744a789). Without it the UPL's
	 * PciHostBridgeLib scans buses 0..255, and Sony's MCFG decodes only 0..0x3F, so buses >= 64
	 * would read IOMMU / LAPIC / other MMIO as config space (upl_hob.h). PS4 Fat (runs 2/3): bus 0
	 * only, BARs as OrbisOS assigned them; Mem covers every 32-bit BAR (Aeolia 0xC4..0xDC, GPU
	 * 0xE0..0xE48, MEM window 0x80000000) and stops before ECAM 0xF8000000, IOMMU 0xFC000000 and
	 * the LAPIC; 64-bit Aeolia 14.0 BAR above 4 GiB. A harness passes its own (in->pci_rb). */
	{
		efi_guid gid = (efi_guid)GUID_UPL_PCI_ROOT_BRIDGES;
		upl_pci_root_bridges *rb = hob_add_guid(&g, gid, sizeof(*rb));
		if (!rb) return "upl: PCI root bridge HOB alloc";
		u_memset(rb, 0, sizeof(*rb));
		rb->header.revision = 1; rb->header.length = sizeof(*rb);
		rb->resource_assigned = 1; rb->count = 1;
		upl_pci_root_bridge *b = &rb->root_bridge[0];
		if (in->pci_rb) {
			u_memcpy(b, in->pci_rb, sizeof(*b));
		} else {
			b->allocation_attributes = 1 | 2;               /* COMBINE_MEM_PMEM | MEM64_DECODE */
			b->bus  = (upl_pci_aperture){ 0, 0, 0 };
			b->io   = (upl_pci_aperture){ 0x1000, 0xffff, 0 };
			b->mem  = (upl_pci_aperture){ 0x80000000ull, 0xf7ffffffull, 0 };
			b->mem_above_4g = (upl_pci_aperture){ 0xfdf8000000ull, 0xfdf9ffffffull, 0 };
			b->pmem = (upl_pci_aperture){ ~0ull, 0, 0 };    /* empty: Base > Limit */
			b->pmem_above_4g = (upl_pci_aperture){ ~0ull, 0, 0 };
			b->hid = 0x0A0341D0u;                           /* EISA PNP0A03 */
			b->uid = 0;
		}
	}

	/* optional: framebuffer (ledger H1) */
	if (in->fb_base) {
		efi_guid gid = (efi_guid)GUID_EFI_GRAPHICS_INFO;
		efi_graphics_info_hob *gi = hob_add_guid(&g, gid, sizeof(*gi));
		if (gi) {
			gi->frame_buffer_base = in->fb_base;
			gi->frame_buffer_size = (u32)in->fb_size;
			gi->mode.version = 0;
			gi->mode.horizontal_resolution = in->fb_w;
			gi->mode.vertical_resolution   = in->fb_h;
			gi->mode.pixel_format = PixelBlueGreenRedReserved8BitPerColor;   /* amdgpu BGRX */
			gi->mode.pixels_per_scan_line = in->fb_stride / (in->fb_bpp / 8);
		}
	}

	/* optional: PS4 directboot info (our GUID) */
	if (in->ps4) {
		efi_guid gid = (efi_guid)GUID_PS4_DIRECTBOOT_INFO;
		ps4_directboot_info *pi = hob_add_guid(&g, gid, sizeof(*pi));
		if (pi) u_memcpy(pi, in->ps4, sizeof(*pi));
	}

	/* END, then fix up PHIT's end pointer + free window start. */
	hob_header *end = hob_add(&g, HOB_TYPE_END_OF_HOB_LIST, sizeof(hob_header));
	if (!end) return "upl: END HOB alloc";
	u64 end_addr = hob_area + g.cur;
	phit->end_of_hob_list    = end_addr;
	phit->free_memory_bottom = align_up(end_addr, 0x1000);

	/* ---- step 5: jump (ledger G1): RCX = HOB list ---- */
	if (in->progress) in->progress(in->progress_ctx, 4);
#ifdef UPL_HOST_TEST
	in->test_entry = bias + eh->entry; in->test_hobs = hob_area; in->test_stack = stack_top;
	return 0;
#endif
	jump_to_upl(bias + eh->entry, hob_area, stack_top);
	return "upl: jump returned";   /* unreachable */
}
