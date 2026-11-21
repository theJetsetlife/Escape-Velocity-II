/* storage.c - SATA (AHCI) and SDHCI. Standard functions at level 2: AHCI generic host registers
 * CAP..CAP2 (ABAR = BAR5), SDHCI capabilities 0x40-0x4b + version 0xfc (BAR0). Sony functions
 * are identified only. Nothing touches a port, a command list or the medium. */
#include "../diag.h"

void dx_storage(struct prb_ctx *c)
{
    if (!c->ecam_ok) { diag(c, D_SATA, ST_UNSUPPORTED, "no PCI scan"); diag(c, D_SDHCI, ST_UNSUPPORTED, "no PCI scan"); return; }
    u32 sata = 0, sd = 0; int sony;
    for (u32 i = 0; i < c->ndev; i++) {
        u32 k = dev_class(&c->dev[i], &sony);
        if (k == DC_AHCI) { sata++; if (!sony && c->cfg.level >= LVL_DOCUMENTED) dev_caps(c, &c->dev[i], 0x24, 0, 0x28); }
        if (k == DC_SDHCI) {
            sd++;
            if (!sony && c->cfg.level >= LVL_DOCUMENTED) { dev_caps(c, &c->dev[i], 0x10, 0x40, 0xc); dev_caps(c, &c->dev[i], 0x10, 0xfc, 4); }
        }
    }
    diag(c, D_SATA, sata ? ST_OK : ST_UNSUPPORTED, sata ? "AHCI controller(s) identified" : "no AHCI controller found");
    diag(c, D_SDHCI, sd ? ST_OK : ST_UNSUPPORTED, sd ? "SDHCI controller(s) identified" : "no SDHCI controller found");
}
