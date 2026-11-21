/* sbregs.c - the selected backend's identity registers, then its allowlisted windows (each needs its level). */
#include "../diagnostics/diag.h"
#include "sb.h"

void dx_sb_regs(struct prb_ctx *c)
{
    const struct sb_backend *b = sb_by_family(c->id.sb_family);
    if (!b) { diag(c, D_SB_REGS, ST_SKIPPED_SAFETY, "no backend for this southbridge: no register allowlist"); return; }
    if (!c->ecam_ok) { diag(c, D_SB_REGS, ST_UNSUPPORTED, "no PCI access"); return; }
    u32 done = 0, held = 0;

    if (b->chipid_level <= c->cfg.level) {
        u64 bar = prb_bar(c, c->id.glue_bus, c->id.glue_dev, c->id.glue_fn, b->chipid_bar);
        int ok = bar != 0;
        for (u32 k = 0; k < 3 && ok; k++) ok = prb_mmio_ok(c, bar + b->chipid_off[k], 4);
        if (ok) {
            u32 v[6];
            for (u32 k = 0; k < 3; k++) {
                c->id.sb_chipid[k] = hal_mmio32(c, bar + b->chipid_off[k]);
                v[2 * k] = b->chipid_off[k]; v[2 * k + 1] = c->id.sb_chipid[k];
            }
            rec(c, T_MMIO32, bar, RG_SB_CHIPID, v, sizeof v);      /* (offset, value) pairs */
            c->id.chipid_ok = 1;
            done++;
        } else rec_s(c, T_NOTE, b->chipid_bar, bar, "identity registers not readable (BAR empty or not UC)");
    } else held++;

    for (u32 w = 0; w < b->nwin; w++) {
        const struct sb_window *win = &b->win[w];
        if (win->level > c->cfg.level) { held++; continue; }
        u64 bar = prb_bar(c, c->id.glue_bus, c->id.glue_dev, c->id.glue_fn, win->bar);
        if (!bar) { rec_s(c, T_NOTE, win->bar, w, "glue BAR empty"); continue; }
        prb_dump_mmio(c, bar + win->off, win->len, win->region);
        done++;
    }
    rec_s(c, T_NOTE, done, held, "southbridge reads done | held back by read level");
    if (!done) diag(c, D_SB_REGS, held ? ST_SKIPPED_SAFETY : ST_FAILED,
                    held ? "every register read of this backend needs a higher level" : "glue BARs not readable");
    else diag(c, D_SB_REGS, ST_OK, held ? "identity + allowlisted windows (some held back by level)" : "identity + allowlisted windows");
}
