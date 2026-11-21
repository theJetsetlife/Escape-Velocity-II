/* dmap.c - the direct map is used only after it proves itself: the PML4 read through
 * dmap + CR3 must hold the kernel's recursive slot PML4[PML4PML4I] pointing back at CR3. Only the
 * real PML4 page has that property (src/diag gate 6, hardware run 2). */
#include "../fw.h"
#include "../../core/hal.h"
#include "../../logging/rec.h"
#include "fwsel.h"

#define PA_MASK 0x000ffffffffff000ull

u32 fw_dmap_prove(struct prb_ctx *c)
{
    if (!c->fwrow) { diag(c, D_DMAP_PROOF, ST_UNSUPPORTED, "no accepted DMAP profile for this kernel"); return 0; }
    const struct db_kofs *k = c->fwrow;
    const u8 *kb = hal_kern(c->kbase);
    u32 pml4i = *(const u32 *)(kb + k->pml4pml4i), dml4 = *(const u32 *)(kb + k->dmpml4i);
    u32 ddpi = *(const u32 *)(kb + k->dmpdpi);
    u32 x[4]; hal_cpuid(0x80000008, 0, x);
    c->maxpa = x[0] & 0xff;
    c->cr3 = hal_rdcr(3);
    if (dml4 < 256 || dml4 > 511 || ddpi > 511 || pml4i < 256 || pml4i > 511 || c->maxpa < 32 || c->maxpa > 52) {
        diag(c, D_DMAP_PROOF, ST_FAILED, "DMAP globals / MAXPHYADDR implausible");
        return 0;
    }
    u64 dmap = 0xffff800000000000ull | (u64)dml4 << 39 | (u64)ddpi << 30;
    c->dmap = dmap;
    const u64 *pml4 = hal_phys(c, c->cr3 & PA_MASK, 4096);
    u64 self = pml4[pml4i & 511];
    rec_s(c, T_NOTE, pml4i, self, "pml4[pml4pml4i]");
    if (!(self & 1) || (self & PA_MASK) != (c->cr3 & PA_MASK)) {
        c->dmap = 0;
        diag(c, D_DMAP_PROOF, ST_FAILED, "PML4[PML4PML4I] via DMAP does not point back at CR3");
        return 0;
    }
    rec_s(c, T_NOTE, dmap, c->cr3, "dmap|cr3");
    diag(c, D_DMAP_PROOF, ST_OK, "direct map proven");
    return 1;
}
