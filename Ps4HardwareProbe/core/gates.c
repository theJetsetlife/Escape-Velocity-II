/* gates.c - safety gates (generalised from src/diag/ring0.c, hardware-proven on 9.00):
 *   RAM : the DMAP VA must be present in the live page tables
 *   MMIO: additionally the effective memory type must be UC (MTRR UC and PAT not WC)
 * Requires c->dmap proven (firmware/validation). MTRR MSRs are read only on family 16h. */
#include "hal.h"
#include "../logging/rec.h"

#define PA_MASK 0x000ffffffffff000ull

int prb_memeq(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}

int prb_walk(const struct prb_ctx *c, u64 va, u64 *leaf)
{
    const u64 *t = hal_phys(c, c->cr3 & PA_MASK, 4096);
    u64 e = t[(va >> 39) & 511];
    if (!(e & 1)) return 0;
    t = hal_phys(c, e & PA_MASK, 4096);
    e = t[(va >> 30) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) { *leaf = e; return 3; }
    t = hal_phys(c, e & PA_MASK, 4096);
    e = t[(va >> 21) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) { *leaf = e; return 2; }
    t = hal_phys(c, e & PA_MASK, 4096);
    e = t[(va >> 12) & 511];
    if (!(e & 1)) return 0;
    *leaf = e;
    return 1;
}

static u64 lvl_size(int l) { return l == 3 ? 1ull << 30 : l == 2 ? 1ull << 21 : 1ull << 12; }

int prb_ram_ok(const struct prb_ctx *c, u64 pa, u64 n)
{
    if (!c->dmap || !n || pa + n < pa || pa + n > (1ull << c->maxpa)) return 0;
    u64 p = pa;
    while (p < pa + n) {
        u64 leaf; int l = prb_walk(c, c->dmap + p, &leaf);
        if (!l) return 0;
        u64 sz = lvl_size(l);
        p = (p & ~(sz - 1)) + sz;
    }
    return 1;
}

u32 prb_mtrr_type(const struct prb_ctx *c, u64 pa)
{
    u64 def = hal_rdmsr(0x2ff);
    if (!(def & 0x800)) return 0;
    u32 vcnt = (u32)hal_rdmsr(0xfe) & 0xff;
    u64 m_all = ((1ull << c->maxpa) - 1) & ~0xfffull;
    int hit = 0; u32 type = 0xff;
    for (u32 i = 0; i < vcnt && i < 16; i++) {
        u64 base = hal_rdmsr(0x200 + 2 * i), mask = hal_rdmsr(0x201 + 2 * i);
        if (!(mask & 0x800)) continue;
        u64 m = mask & m_all;
        if ((pa & m) == (base & m)) {
            u32 t = base & 0xff;
            if (t == 0) return 0;
            hit = 1; type = t;
        }
    }
    if (hit) return type;
    u64 sc = hal_rdmsr(0xc0010010);                    /* SYSCFG Tom2ForceMemTypeWB */
    if ((sc & (1ull << 21)) && (sc & (1ull << 22)) &&
        pa >= (1ull << 32) && pa < (hal_rdmsr(0xc001001d) & 0x0000ffffff800000ull))
        return 6;
    return (u32)(def & 0xff);
}

u32 prb_pat_type(u64 leaf, int lvl)
{
    u32 idx = ((leaf >> 3) & 1) | (((leaf >> 4) & 1) << 1) |
              (((lvl == 1 ? (leaf >> 7) : (leaf >> 12)) & 1) << 2);
    return (u32)(hal_rdmsr(0x277) >> (idx * 8)) & 7;
}

int prb_mmio_ok(struct prb_ctx *c, u64 pa, u64 n)
{
    if (!c->fam16h) { rec_s(c, T_NOTE, pa, 0, "mmio refused: memory type unknown (not family 16h)"); return 0; }
    if (!prb_ram_ok(c, pa, n)) { rec_s(c, T_NOTE, pa, n, "mmio refused: not mapped in DMAP"); return 0; }
    for (u64 p = pa & ~0xfffull; p < pa + n; p += 0x1000) {
        u64 leaf; int l = prb_walk(c, c->dmap + p, &leaf);
        if (prb_mtrr_type(c, p) != 0 || prb_pat_type(leaf, l) == 1) {
            rec_s(c, T_NOTE, p, 0, "mmio refused: not uncacheable");
            return 0;
        }
    }
    return 1;
}

void prb_dump_mmio(struct prb_ctx *c, u64 pa, u32 bytes, u64 region)
{
    if (!prb_mmio_ok(c, pa, bytes)) return;
    u32 *d = rec_begin(c, T_MMIO32, pa, region, bytes);
    if (!d) return;
    for (u32 i = 0; i < bytes / 4; i++) d[i] = hal_mmio32(c, pa + i * 4);
}

/* ECAM only: CF8/CFC would write the address latch and race OrbisOS's own config cycles. */
u32 prb_cfg32(const struct prb_ctx *c, u32 bus, u32 dev, u32 fn, u32 off)
{
    if (!c->ecam_ok || bus >= c->ecam_buses) return 0xffffffffu;
    return hal_mmio32(c, c->ecam + ((u64)bus << 20 | dev << 15 | fn << 12 | (off & 0xffc)));
}

u64 prb_bar(const struct prb_ctx *c, u32 bus, u32 dev, u32 fn, u32 off)
{
    u32 lo = prb_cfg32(c, bus, dev, fn, off);
    if ((lo & 1) || lo == 0xffffffffu) return 0;
    u64 base = lo & ~0xfull;
    if (((lo >> 1) & 3) == 2 && off < 0x24) base |= (u64)prb_cfg32(c, bus, dev, fn, off + 4) << 32;
    return base;
}
