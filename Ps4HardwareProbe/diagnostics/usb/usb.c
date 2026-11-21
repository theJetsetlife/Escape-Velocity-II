/* usb.c - USB host controllers. Standard xHCI/EHCI functions: capability registers at level 2
 * (CAPLENGTH/HCIVERSION, HCSPARAMS1-3, HCCPARAMS1: read-only by spec). Sony xHCI functions (one
 * function, several controllers, vendor layout): identified only. */
#include "../diag.h"

void dx_usb(struct prb_ctx *c)
{
    if (!c->ecam_ok) { diag(c, D_USB, ST_UNSUPPORTED, "no PCI scan"); return; }
    u32 n = 0, sonyn = 0, capped = 0; int sony;
    for (u32 i = 0; i < c->ndev; i++) {
        u32 k = dev_class(&c->dev[i], &sony);
        if (k != DC_XHCI && k != DC_EHCI) continue;
        n++;
        if (sony) { sonyn++; continue; }
        if (c->cfg.level >= LVL_DOCUMENTED) { dev_caps(c, &c->dev[i], 0x10, 0, k == DC_XHCI ? 0x20 : 0x10); capped++; }
    }
    rec_s(c, T_NOTE, n, (u64)sonyn | (u64)capped << 32, "usb controllers | sony, caps read<<32");
    if (!n) diag(c, D_USB, ST_UNSUPPORTED, "no USB controller found");
    else if (sonyn == n) diag(c, D_USB, ST_OK, "Sony USB function(s) identified; vendor register layout not allowlisted");
    else diag(c, D_USB, ST_OK, capped ? "USB controllers identified, capability registers read" : "USB controllers identified");
}
