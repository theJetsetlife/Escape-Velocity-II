/* timers.c - TSC (two ring0 samples around a userland sleep, with CLOCK_MONOTONIC from userland:
 * the host computes the rate) and HPET (ACPI HPET table base; caps, config, main counter; level 2). */
#include "../diag.h"
#include "../../southbridge/sb.h"

void dx_tsc(struct prb_ctx *c, u32 sample)
{
    rec(c, T_TSC, sample, hal_rdtsc(), 0, 0);
    if (sample) diag(c, D_TSC, ST_OK, "two TSC samples; rate computed host-side against CLOCK_MONOTONIC");
}

void dx_hpet(struct prb_ctx *c)
{
    if (!c->hpet) { diag(c, D_HPET, ST_UNSUPPORTED, "no ACPI HPET table"); return; }
    if (c->cfg.level < LVL_DOCUMENTED) { diag(c, D_HPET, ST_SKIPPED_SAFETY, "HPET MMIO needs read level 2"); return; }
    if (!prb_mmio_ok(c, c->hpet, 0x100)) { diag(c, D_HPET, ST_SKIPPED_SAFETY, "HPET page not mapped UC"); return; }
    prb_dump_mmio(c, c->hpet, 0x18, RG_HPET);
    prb_dump_mmio(c, c->hpet + 0xf0, 8, RG_HPET);
    diag(c, D_HPET, ST_OK, "HPET capabilities, config, main counter");
}
