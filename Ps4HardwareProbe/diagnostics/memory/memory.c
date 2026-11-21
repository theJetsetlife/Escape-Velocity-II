/* memory.c - variable MTRRs and the run-length map of the direct map (0..16 GiB, page tables only).
 * The OS memory map comes from userland (machdep.smap, vm.phys_segs). */
#include "../diag.h"

#define PA_MASK 0x000ffffffffff000ull

void dx_mtrr(struct prb_ctx *c)
{
    if (!c->fam16h) { diag(c, D_MEM_MTRR, ST_SKIPPED_SAFETY, "not family 16h"); return; }
    u32 vcnt = (u32)hal_rdmsr(0xfe) & 0xff;
    for (u32 i = 0; i < vcnt && i < 16; i++) {
        rec(c, T_MSR, 0x200 + 2 * i, hal_rdmsr(0x200 + 2 * i), 0, 0);
        rec(c, T_MSR, 0x201 + 2 * i, hal_rdmsr(0x201 + 2 * i), 0, 0);
    }
    diag(c, D_MEM_MTRR, ST_OK, "variable MTRRs");
}

static u64 leaf_key(u64 e, int lvl)
{
    if (!lvl) return 0;
    u64 pat = lvl == 1 ? (e >> 7) & 1 : (e >> 12) & 1;
    return 1 | ((e >> 1) & 1) << 1 | ((e >> 2) & 1) << 2 | ((e >> 3) & 1) << 3 |
           ((e >> 4) & 1) << 4 | pat << 5 | (e >> 63) << 6 | (u64)lvl << 8;
}

void dx_dmap_runs(struct prb_ctx *c)
{
    const u64 end = 16ull << 30;
    u64 run_start = 0, run_key = ~0ull, pa = 0;
    while (pa < end) {
        u64 leaf = 0; int l = prb_walk(c, c->dmap + pa, &leaf);
        u64 key = leaf_key(leaf, l);
        u64 step = l == 3 ? 1ull << 30 : l == 2 ? 1ull << 21 : 1ull << 12;
        if (!l) {
            u64 va = c->dmap + pa;
            const u64 *t = hal_phys(c, c->cr3 & PA_MASK, 4096);
            u64 e = t[(va >> 39) & 511];
            if (!(e & 1)) step = 1ull << 39;
            else {
                t = hal_phys(c, e & PA_MASK, 4096); e = t[(va >> 30) & 511];
                if (!(e & 1)) step = 1ull << 30;
                else { t = hal_phys(c, e & PA_MASK, 4096); e = t[(va >> 21) & 511]; if (!(e & 1)) step = 1ull << 21; }
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
    diag(c, D_MEM_DMAP, ST_OK, "direct-map coverage 0..16 GiB");
}
