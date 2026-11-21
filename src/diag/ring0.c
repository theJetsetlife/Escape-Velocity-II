/* Ring0 half of the read-only diagnostic payload.
 *
 * Reached through syscall 11: the 9.00 kpatch points sysent[11].sy_call at a
 * `jmp qword ptr [rsi]` gadget (LEDGER A1/A3), so the kernel enters
 * ring0_main(td, uap) with uap[0] = this function and uap[1] = our ctx.
 *
 * READ-ONLY CONTRACT: this file never writes to a hardware register, a PCI config
 * register, an MSR, a control register or kernel memory. The only stores go into
 * the caller's mlock'd record buffer. Every physical access is checked first:
 *   - RAM:  the direct-map VA must be present in the live page tables (page walk)
 *   - MMIO: additionally the effective memory type must be uncacheable (MTRR + PAT)
 * Anything that fails a check is skipped and reported, never touched.
 *
 * Evidence for every constant: rebuild/LEDGER.md (ids in brackets).
 */
#include "rec.h"

/* ---- primitives --------------------------------------------------------- */
static inline u64 rdmsr(u32 m)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m));
    return ((u64)hi << 32) | lo;
}
#define RDCR(n) ({ u64 v; __asm__ volatile("mov %%cr" #n ", %0" : "=r"(v)); v; })
static inline void cpuid(u32 leaf, u32 sub, u32 r[4])
{
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                     : "a"(leaf), "c"(sub));
}
static inline u64 irq_save(void)
{
    u64 f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(u64 f)
{
    __asm__ volatile("pushq %0; popfq" :: "r"(f) : "memory", "cc");
}

#define PA_MASK   0x000ffffffffff000ull

/* ---- 9.00 constants (all GREEN in LEDGER) ------------------------------- */
#define OFF_XFAST       0x1c0       /* A5: LSTAR = kbase + 0x1c0             */
#define OFF_GADGET      0x4c7ad     /* A1: FF 26                             */
#define OFF_ICCQ        0x2e1760    /* D1/D3 prologue                        */
#define OFF_PML4PML4I   0x1b904a0   /* A6: u32 globals read by pmap code     */
#define OFF_DMPML4I     0x1b904a4
#define OFF_DMPDPI      0x1b904a8

static const u8 sig_xfast[12] = { 0x0f,0x01,0xf8,0x65,0x48,0x89,0x24,0x25,0xa8,0x02,0x00,0x00 };
static const u8 sig_gadget[2] = { 0xff,0x26 };
static const u8 sig_iccq[8]   = { 0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56 };

static int memeq(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}

/* ---- safety gates -------------------------------------------------------- */

/* Walk the live page tables for va. Returns the level of the leaf
 * (1 = 4K, 2 = 2M, 3 = 1G) or 0 if not present. Page-table pages are
 * reached through the DMAP, exactly as the FreeBSD pmap itself does. */
static int walk(struct diag_ctx *c, u64 va, u64 *leaf)
{
    u64 *t = (u64 *)(c->dmap + (c->cr3 & PA_MASK));
    u64 e = t[(va >> 39) & 511];
    if (!(e & 1)) return 0;
    t = (u64 *)(c->dmap + (e & PA_MASK));
    e = t[(va >> 30) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) { *leaf = e; return 3; }
    t = (u64 *)(c->dmap + (e & PA_MASK));
    e = t[(va >> 21) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) { *leaf = e; return 2; }
    t = (u64 *)(c->dmap + (e & PA_MASK));
    e = t[(va >> 12) & 511];
    if (!(e & 1)) return 0;
    *leaf = e;
    return 1;
}

static u64 lvl_size(int l) { return l == 3 ? 1ull << 30 : l == 2 ? 1ull << 21 : 1ull << 12; }

/* phys range [pa, pa+n) entirely present in the DMAP */
static int ram_ok(struct diag_ctx *c, u64 pa, u64 n)
{
    if (!n || pa + n < pa || (pa + n) > (1ull << c->maxpa)) return 0;
    u64 p = pa;
    while (p < pa + n) {
        u64 leaf; int l = walk(c, c->dmap + p, &leaf);
        if (!l) return 0;
        u64 sz = lvl_size(l);
        p = (p & ~(sz - 1)) + sz;
    }
    return 1;
}

/* MTRR memory type for pa (variable ranges; MMIO lives above 1 MiB). 0 = UC. */
static u32 mtrr_type(struct diag_ctx *c, u64 pa)
{
    u64 def = rdmsr(0x2ff);
    if (!(def & 0x800)) return 0;                      /* MTRRs disabled -> UC */
    u32 vcnt = (u32)rdmsr(0xfe) & 0xff;
    u64 m_all = ((1ull << c->maxpa) - 1) & ~0xfffull;
    int hit = 0; u32 type = 0xff;
    for (u32 i = 0; i < vcnt && i < 16; i++) {
        u64 base = rdmsr(0x200 + 2 * i), mask = rdmsr(0x201 + 2 * i);
        if (!(mask & 0x800)) continue;
        u64 m = mask & m_all;
        if ((pa & m) == (base & m)) {
            u32 t = base & 0xff;
            if (t == 0) return 0;                      /* UC wins any overlap */
            hit = 1; type = t;
        }
    }
    if (hit) return type;
    /* AMD SYSCFG: MtrrTom2En (bit 21) + Tom2ForceMemTypeWB (bit 22) make 4 GiB..TOM2
     * default to WB instead of MTRRdefType (HW run 2: SYSCFG 0x760000). */
    u64 sc = rdmsr(0xc0010010);
    if ((sc & (1ull << 21)) && (sc & (1ull << 22)) &&
        pa >= (1ull << 32) && pa < (rdmsr(0xc001001d) & 0x0000ffffff800000ull))
        return 6;
    return (u32)(def & 0xff);
}

/* PAT type selected by a leaf entry */
static u32 pat_type(u64 leaf, int lvl)
{
    u32 idx = ((leaf >> 3) & 1) | (((leaf >> 4) & 1) << 1) |
              (((lvl == 1 ? (leaf >> 7) : (leaf >> 12)) & 1) << 2);
    return (u32)(rdmsr(0x277) >> (idx * 8)) & 7;
}

/* MMIO range is mapped AND effectively uncacheable (MTRR UC, PAT not WC). */
static int mmio_ok(struct diag_ctx *c, u64 pa, u64 n)
{
    if (!ram_ok(c, pa, n)) {
        rec(c, T_ERR, E_NOTMAPPED, pa, 0, 0);
        return 0;
    }
    for (u64 p = pa & ~0xfffull; p < pa + n; p += 0x1000) {
        u64 leaf; int l = walk(c, c->dmap + p, &leaf);
        if (mtrr_type(c, p) != 0 || pat_type(leaf, l) == 1) {
            rec(c, T_ERR, E_NOTUC, p, 0, 0);
            return 0;
        }
    }
    return 1;
}

static inline u32 mmio32(struct diag_ctx *c, u64 pa)
{
    return *(volatile u32 *)(c->dmap + pa);
}

static void dump_mmio(struct diag_ctx *c, u64 pa, u32 bytes, u64 region)
{
    if (!mmio_ok(c, pa, bytes)) return;
    u32 *d = rec_begin(c, T_MMIO32, pa, region, bytes);
    if (!d) return;
    for (u32 i = 0; i < bytes / 4; i++) d[i] = mmio32(c, pa + i * 4);
}

static void dump_ram(struct diag_ctx *c, u64 pa, u32 bytes, u64 kind)
{
    if (!ram_ok(c, pa, bytes)) { rec(c, T_ERR, E_NOTMAPPED, pa, 0, 0); return; }
    u8 *d = rec_begin(c, T_PHYS, pa, kind, bytes);
    if (d) memcpy(d, (void *)(c->dmap + pa), bytes);
}

/* ---- PCI config (read only) --------------------------------------------- */
static u32 cfg_rd(struct diag_ctx *c, u32 bus, u32 dev, u32 fn, u32 off)
{
    /* ECAM only. No CF8/CFC fallback: that writes the address latch (a hardware write)
     * and can race OrbisOS's own config cycles on another core. */
    if (!c->ecam_ok || bus) return 0xffffffffu;
    return mmio32(c, c->ecam + ((u64)bus << 20 | dev << 15 | fn << 12 | (off & 0xffc)));
}

/* memory BAR at config offset `off` (0x10..0x24) -> CPU physical base, 0 if none */
static u64 bar_mem(struct diag_ctx *c, u32 dev, u32 fn, u32 off)
{
    u32 lo = cfg_rd(c, 0, dev, fn, off);
    if (lo & 1) return 0;                               /* I/O BAR */
    u64 base = lo & ~0xfull;
    if (((lo >> 1) & 3) == 2 && off < 0x24)             /* 64-bit BAR */
        base |= (u64)cfg_rd(c, 0, dev, fn, off + 4) << 32;
    return base;
}

/* ---- phases --------------------------------------------------------------- */

static int ph_ident(struct diag_ctx *c)
{
    struct diag_regs r;
    memset(&r, 0, sizeof r);
    r.lstar = rdmsr(0xc0000082);
    r.kbase = r.lstar - OFF_XFAST;
    const u8 *kb = (const u8 *)r.kbase;

    int g1 = (r.kbase & 0x3fff) == 0 && (r.kbase >> 32) == 0xffffffffull;
    int g2 = g1 && memeq(kb + OFF_XFAST, sig_xfast, sizeof sig_xfast);
    int g3 = g2 && memeq(kb + OFF_GADGET, sig_gadget, sizeof sig_gadget);
    int g4 = g3 && memeq(kb + OFF_ICCQ, sig_iccq, sizeof sig_iccq);
    rec_s(c, T_GATE, 1, g1, "kbase aligned, kernel half");
    rec_s(c, T_GATE, 2, g2, "Xfast_syscall swapgs prologue @kbase+0x1c0");
    rec_s(c, T_GATE, 3, g3, "jmp [rsi] gadget @kbase+0x4c7ad");
    rec_s(c, T_GATE, 4, g4, "icc_query_nowait prologue @kbase+0x2e1760");
    if (!g4) { rec(c, T_ERR, E_FW_GATE, r.kbase, 0, 0); return 0; }

    r.pml4pml4i = *(const u32 *)(kb + OFF_PML4PML4I);
    r.dmpml4i   = *(const u32 *)(kb + OFF_DMPML4I);
    r.dmpdpi    = *(const u32 *)(kb + OFF_DMPDPI);
    u32 x[4];
    cpuid(0x80000008, 0, x);
    r.cpu_maxpa = x[0] & 0xff;
    r.cr3 = RDCR(3);
    int g5 = r.dmpml4i >= 256 && r.dmpml4i < 512 && r.dmpdpi < 512 &&
             r.cpu_maxpa >= 32 && r.cpu_maxpa <= 52;
    rec_s(c, T_GATE, 5, g5, "DMPML4I/DMPDPI/MAXPHYADDR plausible");
    if (!g5) { rec(c, T_REGS, 0, 0, &r, sizeof r); rec(c, T_ERR, E_DMAP, 0, 0, 0); return 0; }

    r.dmap = 0xffff800000000000ull | (u64)r.dmpml4i << 39 | (u64)r.dmpdpi << 30;
    c->kbase = r.kbase; c->dmap = r.dmap; c->cr3 = r.cr3; c->maxpa = r.cpu_maxpa;

    /* Self-check: translate the kernel's own text VA through our walker and read
     * the same bytes back through the DMAP. Proves dmap + walker + cr3 agree. */
    u64 leaf; int l = walk(c, r.kbase + OFF_XFAST, &leaf);
    /* Gate 6 (the proof): the PML4 read through dmap+CR3 must contain the kernel's
     * recursive slot PML4[PML4PML4I] pointing back at CR3. Only the real PML4 page
     * has that property, so this proves the DMAP base. Hardware run 1: the
     * kernel-text alias check below failed while the walk itself returned a valid
     * kernel-text leaf, so the alias check is now informational only. */
    const u64 *pml4 = (const u64 *)(r.dmap + (r.cr3 & PA_MASK));
    u64 self = pml4[r.pml4pml4i & 511];
    int g6 = (self & 1) && (self & PA_MASK) == (r.cr3 & PA_MASK);
    rec_s(c, T_NOTE, r.pml4pml4i, self, "pml4[pml4pml4i]");
    rec_s(c, T_GATE, 6, g6, "DMAP proof: PML4[PML4PML4I] via DMAP points back at CR3");

    /* informational: kernel text through its DMAP alias, every sub-step logged */
    if (l) {
        u64 sz = lvl_size(l);
        u64 pa = (leaf & PA_MASK & ~(sz - 1)) | ((r.kbase + OFF_XFAST) & (sz - 1));
        rec(c, T_PTE, r.kbase + OFF_XFAST, leaf, &(u64){ (u64)l }, 8);
        u64 dleaf = 0; u64 dl = (u64)walk(c, r.dmap + pa, &dleaf);
        rec(c, T_PTE, r.dmap + pa, dleaf, &dl, 8);              /* DMAP leaf for that pa */
        if (g6 && dl && ram_ok(c, pa, 16))
            rec(c, T_PHYS, pa, PK_KTEXT | (u64)memeq((const void *)(r.dmap + pa), sig_xfast,
                sizeof sig_xfast) << 16, (const void *)(r.dmap + pa), 16);   /* b>>16 = match */
    }
    rec(c, T_REGS, 0, 0, &r, sizeof r);
    if (!g6) { rec(c, T_ERR, E_DMAP, r.dmap, 0, 0); c->dmap = 0; return 0; }
    return 1;
}

static void msr(struct diag_ctx *c, u32 m) { rec(c, T_MSR, m, rdmsr(m), 0, 0); }

static int ph_cpu(struct diag_ctx *c)
{
    struct diag_regs r;
    memset(&r, 0, sizeof r);
    r.kbase = c->kbase; r.dmap = c->dmap; r.lstar = rdmsr(0xc0000082);
    r.cr0 = RDCR(0); r.cr3 = RDCR(3); r.cr4 = RDCR(4);
    r.efer = rdmsr(0xc0000080);
    __asm__ volatile("pushfq; popq %0" : "=r"(r.rflags));
    u32 x[4];
    cpuid(1, 0, x); r.cpuid1_eax = x[0]; r.cpuid1_ebx = x[1]; r.cpuid1_ecx = x[2]; r.cpuid1_edx = x[3];
    r.apic_id = x[1] >> 24;
    cpuid(7, 0, x); r.cpuid7_ebx = x[1];
    r.cpu_maxpa = c->maxpa;
    rec(c, T_REGS, 1, 0, &r, sizeof r);

    /* architectural + AMD family 16h MSRs that exist on this core (H4) */
    static const u32 list[] = {
        0x1b,        /* APIC_BASE        */
        0xfe,        /* MTRRcap          */
        0x2ff,       /* MTRRdefType      */
        0x250, 0x258, 0x259, 0x268, 0x269, 0x26a, 0x26b, 0x26c, 0x26d, 0x26e, 0x26f,
        0x277,       /* PAT              */
        0xc0010010,  /* SYSCFG           */
        0xc0010015,  /* HWCR             */
        0xc001001a,  /* TOP_MEM          */
        0xc001001d,  /* TOM2             */
        0xc0010058,  /* MMIO_CFG_BASE (ECAM) */
    };
    for (u32 i = 0; i < sizeof list / sizeof list[0]; i++) msr(c, list[i]);
    u32 vcnt = (u32)rdmsr(0xfe) & 0xff;
    for (u32 i = 0; i < vcnt && i < 16; i++) { msr(c, 0x200 + 2 * i); msr(c, 0x201 + 2 * i); }

    /* ECAM base from the CPU itself, not from Linux's view */
    u64 mc = rdmsr(0xc0010058);
    c->ecam = mc & 0x0000fffffff00000ull;
    /* bus 0 = 32 dev x 8 fn x 4 KiB = 1 MiB of ECAM */
    c->ecam_ok = (mc & 1) && c->ecam && mmio_ok(c, c->ecam, 1u << 20);
    if (!c->ecam_ok) rec(c, T_ERR, E_NOECAM, mc, 0, 0);
    return 1;
}

static int ph_pgtbl(struct diag_ctx *c)
{
    u64 pml4 = c->cr3 & PA_MASK;
    dump_ram(c, pml4, 0x1000, PK_PML4);
    u64 *t = (u64 *)(c->dmap + pml4);
    u32 slot = (c->dmap >> 39) & 511;
    for (u32 s = slot; s < slot + 2 && s < 512; s++)
        if (t[s] & 1) dump_ram(c, t[s] & PA_MASK, 0x1000, PK_PDPT | (u64)s << 16);

    /* how is each address of interest mapped in the DMAP? */
    static const u64 probe[] = {
        0x0, 0xe0000, 0x100000, 0x7f000000, 0x80000000, 0xd0200000, 0xd9000000,
        0xe0000000, 0xe4800000, 0xf8000000, 0xfc000000, 0xfee00000,
        0x100000000ull, 0x140000000ull, 0x27f000000ull,
    };
    for (u32 i = 0; i < sizeof probe / sizeof probe[0]; i++) {
        u64 leaf = 0; u64 l = walk(c, c->dmap + probe[i], &leaf);
        rec(c, T_PTE, probe[i], leaf, &l, 8);
        if (l) rec_s(c, T_NOTE, probe[i], mtrr_type(c, probe[i]) | (u64)pat_type(leaf, (int)l) << 8,
                     "memtype mtrr|pat<<8");
    }
    return 1;
}

static int ph_pci(struct diag_ctx *c)
{
    if (!c->ecam_ok) { rec(c, T_ERR, E_NOECAM, c->ecam, 0, 0); return 0; }
    for (u32 dev = 0; dev < 32; dev++) {
        u32 nfn = 1;
        for (u32 fn = 0; fn < nfn; fn++) {
            u32 id = cfg_rd(c, 0, dev, fn, 0);
            if (fn == 0 && id != 0xffffffffu && (cfg_rd(c, 0, dev, 0, 0xc) >> 16) & 0x80) nfn = 8;
            if (id == 0xffffffffu || id == 0) continue;
            u32 *d = rec_begin(c, T_PCICFG, dev << 8 | fn, 1, 256);
            if (!d) return 0;
            for (u32 o = 0; o < 256; o += 4) d[o / 4] = cfg_rd(c, 0, dev, fn, o);
        }
    }
    return 1;
}

static int ph_glue(struct diag_ctx *c)
{
    /* glue = 00:14.4 (H2a); regs relative to BAR4, chip id relative to BAR2 (SRC ps4sb.h) */
    if ((cfg_rd(c, 0, 0x14, 4, 0) & 0xffff) != 0x104d) { rec(c, T_ERR, E_NODEV, 0x1404, 0, 0); return 0; }
    u64 bar4 = bar_mem(c, 0x14, 4, 0x20), bar2 = bar_mem(c, 0x14, 4, 0x18);
    if (!bar4) { rec(c, T_ERR, E_NOBAR, 0x140420, 0, 0); return 0; }
    dump_mmio(c, bar4 + 0x1c8000, 0x200, RG_GLUE_BARWIN);  /* 8 funcs x 0x30, mask/addr pairs */
    dump_mmio(c, bar4 + 0x1c8400, 0x200, RG_GLUE_MSI);     /* CONTROL, MASK(+0x4c), ADDR(+0xac), DATA */
    if (bar2) dump_mmio(c, bar2 + 0x1100, 0x10, RG_CHIPID);
    else rec(c, T_ERR, E_NOBAR, 0x140418, 0, 0);
    return 1;
}

static int ph_iommu(struct diag_ctx *c)
{
    /* AMD IOMMU = 00:00.2, capability id 0x0f; base = cap+4 (bits 31:14) | cap+8 << 32 */
    if (cfg_rd(c, 0, 0, 2, 0) == 0xffffffffu) { rec(c, T_ERR, E_NODEV, 0x0002, 0, 0); return 0; }
    u32 p = cfg_rd(c, 0, 0, 2, 0x34) & 0xfc;
    for (u32 n = 0; p && n < 48; n++) {
        u32 h = cfg_rd(c, 0, 0, 2, p);
        if ((h & 0xff) == 0x0f) {
            u64 lo = cfg_rd(c, 0, 0, 2, p + 4), hi = cfg_rd(c, 0, 0, 2, p + 8);
            u64 base = (hi << 32) | (lo & 0xffffc000u);
            rec_s(c, T_NOTE, base, lo & 1, "iommu base|enable");
            if (base) {
                dump_mmio(c, base, 0x80, RG_IOMMU_LO);
                dump_mmio(c, base + 0x2000, 0x40, RG_IOMMU_HI);
            }
            return 1;
        }
        p = (h >> 8) & 0xfc;
    }
    rec(c, T_ERR, E_NODEV, 0x0002, 0, 0);
    return 0;
}

static int ph_gpu(struct diag_ctx *c)
{
    /* Liverpool >= CHIP_BONAIRE -> register BAR is BAR5 (amdgpu_device.c); DCE 8 regs
     * (dce_8_0_d.h) + CRTC offsets (cikd.h); MC aperture regs (gmc_7_1_d.h). dword indices. */
    if ((cfg_rd(c, 0, 1, 0, 0) & 0xffff) != 0x1002) { rec(c, T_ERR, E_NODEV, 0x0100, 0, 0); return 0; }
    u64 mm = bar_mem(c, 1, 0, 0x24);
    if (!mm) { rec(c, T_ERR, E_NOBAR, 0x010024, 0, 0); return 0; }
    static const u16 gmc[] = { 0x809, 0x80c, 0x80d, 0x80e, 0x81a };
    static const u16 crtc_off[6] = { 0x0, 0x300, 0x2600, 0x2900, 0x2c00, 0x2f00 };
    static const u16 grph[] = { 0x1a00, 0x1a01, 0x1a04, 0x1a06, 0x1a07, 0x1a0d, 0x1a0e, 0x1b9c };
    if (!mmio_ok(c, mm, 0x13000)) return 0;
    u32 *d = rec_begin(c, T_MMIO32, mm, RG_GPU_GMC, sizeof gmc / 2 * 4);
    if (d) for (u32 i = 0; i < sizeof gmc / 2; i++) d[i] = mmio32(c, mm + gmc[i] * 4u);
    for (u32 n = 0; n < 6; n++) {
        u32 v[sizeof grph / 2];
        for (u32 i = 0; i < sizeof grph / 2; i++)
            v[i] = mmio32(c, mm + (u64)(grph[i] + crtc_off[n]) * 4);
        rec(c, T_MMIO32, mm, RG_GPU_CRTC | n << 8, v, sizeof v);

        /* read-back of the scanout surface (HW run 2: MC addresses look system-physical).
         * order in grph[]: 0 ENABLE, 2 SURF_LO, 3 PITCH, 4 SURF_HI, 5 X_END, 6 Y_END */
        if (!(v[0] & 1)) continue;
        u64 surf = ((u64)v[4] << 32) | (v[2] & ~0xffu);
        u64 bpp = 4, pitch = (u64)v[3] * bpp, mid = surf + (v[6] / 2) * pitch + (v[5] / 2) * bpp;
        u64 leaf = 0; u64 l = (u64)walk(c, c->dmap + surf, &leaf);
        rec(c, T_PTE, surf, leaf, &l, 8);
        if (surf && ram_ok(c, surf, 256))
            dump_ram(c, surf, 256, PK_FB | (u64)n << 16);
        if (surf && ram_ok(c, mid & ~0xffull, 256))
            dump_ram(c, mid & ~0xffull, 256, PK_FB | (u64)n << 16 | 1ull << 24);
    }
    return 1;
}

static int acpi_sum(const u8 *p, u32 n) { u8 s = 0; for (u32 i = 0; i < n; i++) s += p[i]; return s == 0; }

static void acpi_table(struct diag_ctx *c, u64 pa, u32 depth)
{
    if (!ram_ok(c, pa, 36)) { rec(c, T_ERR, E_NOTMAPPED, pa, 0, 0); return; }
    const u8 *h = (const u8 *)(c->dmap + pa);
    u32 len = *(const u32 *)(h + 4);
    if (len < 36 || len > 0x40000 || !ram_ok(c, pa, len)) { rec(c, T_ERR, E_ACPI, pa, &len, 4); return; }
    dump_ram(c, pa, len, PK_ACPI);
    if (depth) return;
    if (memeq(h, "FACP", 4)) {                         /* FACS @36, DSDT @40, X_DSDT @140 */
        u64 dsdt = *(const u32 *)(h + 40);
        if (len >= 148 && *(const u64 *)(h + 140)) dsdt = *(const u64 *)(h + 140);
        if (dsdt) acpi_table(c, dsdt, 1);
        u64 facs = *(const u32 *)(h + 36);
        if (facs && ram_ok(c, facs, 64)) dump_ram(c, facs, 64, PK_ACPI);
    }
}

static int ph_acpi(struct diag_ctx *c)
{
    u64 rsdp = 0;
    if (c->acpi_root && ram_ok(c, c->acpi_root, 20) &&
        memeq((const void *)(c->dmap + c->acpi_root), "RSD PTR ", 8))
        rsdp = c->acpi_root;
    for (u64 p = 0xe0000; !rsdp && p < 0x100000; p += 16)
        if (ram_ok(c, p, 20) && memeq((const void *)(c->dmap + p), "RSD PTR ", 8) &&
            acpi_sum((const u8 *)(c->dmap + p), 20))
            rsdp = p;
    if (!rsdp) { rec(c, T_ERR, E_NORSDP, c->acpi_root, 0, 0); return 0; }
    const u8 *r = (const u8 *)(c->dmap + rsdp);
    u32 rlen = (r[15] >= 2 && ram_ok(c, rsdp, 36)) ? 36 : 20;
    dump_ram(c, rsdp, rlen, PK_RSDP);

    u64 root = *(const u32 *)(r + 16); u32 esz = 4;
    if (rlen == 36 && *(const u64 *)(r + 24)) { root = *(const u64 *)(r + 24); esz = 8; }
    acpi_table(c, root, 1);
    if (!ram_ok(c, root, 36)) return 0;
    const u8 *t = (const u8 *)(c->dmap + root);
    u32 len = *(const u32 *)(t + 4);
    if (len < 36 || len > 0x1000 || !ram_ok(c, root, len)) return 0;
    for (u32 o = 36; o + esz <= len; o += esz) {
        u64 e = esz == 8 ? *(const u64 *)(t + o) : *(const u32 *)(t + o);
        if (e) acpi_table(c, e, 0);
    }
    return 1;
}

/* Run-length map of the direct map: every phys range 0..16 GiB with its leaf
 * attributes. key = P | RW<<1 | US<<2 | PWT<<3 | PCD<<4 | PAT<<5 | NX<<6 | level<<8,
 * 0 = not mapped. Reads page tables only (through the proven DMAP). */
static u64 leaf_key(u64 e, int lvl)
{
    if (!lvl) return 0;
    u64 pat = lvl == 1 ? (e >> 7) & 1 : (e >> 12) & 1;
    return 1 | ((e >> 1) & 1) << 1 | ((e >> 2) & 1) << 2 | ((e >> 3) & 1) << 3 |
           ((e >> 4) & 1) << 4 | pat << 5 | (e >> 63) << 6 | (u64)lvl << 8;
}

static int ph_dmap(struct diag_ctx *c)
{
    const u64 end = 16ull << 30;
    u64 run_start = 0, run_key = ~0ull, pa = 0;
    while (pa < end) {
        u64 leaf = 0; int l = walk(c, c->dmap + pa, &leaf);
        u64 key = leaf_key(leaf, l);
        u64 step = l ? lvl_size(l) : (1ull << 12);
        if (!l) {            /* skip whole absent upper-level entries quickly */
            u64 va = c->dmap + pa;
            const u64 *t = (const u64 *)(c->dmap + (c->cr3 & PA_MASK));
            u64 e = t[(va >> 39) & 511];
            if (!(e & 1)) step = 1ull << 39;
            else {
                t = (const u64 *)(c->dmap + (e & PA_MASK));
                e = t[(va >> 30) & 511];
                if (!(e & 1)) step = 1ull << 30;
                else {
                    t = (const u64 *)(c->dmap + (e & PA_MASK));
                    e = t[(va >> 21) & 511];
                    if (!(e & 1)) step = 1ull << 21;
                }
            }
            step -= pa & (step - 1);
        }
        if (key != run_key) {
            if (run_key != ~0ull) rec(c, T_RUN, run_start, pa, &run_key, 8);
            run_start = pa; run_key = key;
        }
        pa += step;
    }
    rec(c, T_RUN, run_start, end, &run_key, 8);
    return 1;
}

/* ---- entry (ring0) --------------------------------------------------------- */
__attribute__((used))
int ring0_main(void *td, u64 *uap)
{
    (void)td;
    struct diag_ctx *c = (struct diag_ctx *)uap[1];
    if (c->magic != DIAG_CTX_MAGIC) return 0;
    u64 fl = irq_save();
    int ok = 0;
    if (c->phase != PH_IDENT && !c->dmap) {        /* nothing runs without a verified DMAP */
        rec(c, T_ERR, E_DMAP, c->phase, 0, 0);
    } else switch (c->phase) {
        case PH_IDENT: ok = ph_ident(c); break;
        case PH_CPU:   ok = ph_cpu(c);   break;
        case PH_PGTBL: ok = ph_pgtbl(c); break;
        case PH_PCI:   ok = ph_pci(c);   break;
        case PH_GLUE:  ok = ph_glue(c);  break;
        case PH_IOMMU: ok = ph_iommu(c); break;
        case PH_GPU:   ok = ph_gpu(c);   break;
        case PH_ACPI:  ok = ph_acpi(c);  break;
        case PH_DMAP:  ok = ph_dmap(c);  break;
    }
    if (ok) c->ok |= 1u << c->phase;
    irq_restore(fl);
    return 0;
}
