/* irq.c - local APIC registers (xAPIC MMIO, LVL_DOCUMENTED: never read on a PS4 by this project),
 * MSI and IOAPIC. MSI capabilities and the MADT are decoded host-side from config space / ACPI. */
#include "../diag.h"
#include "../../southbridge/sb.h"

void dx_irq(struct prb_ctx *c)
{
    if (!c->fam16h || !c->dmap) diag(c, D_IRQ_LAPIC, ST_UNSUPPORTED, "needs family 16h and a proven direct map");
    else if (c->cfg.level < LVL_DOCUMENTED) diag(c, D_IRQ_LAPIC, ST_SKIPPED_SAFETY, "LAPIC MMIO needs read level 2");
    else {
        u64 ab = hal_rdmsr(0x1b);
        if (!(ab & (1u << 11)) || (ab & (1u << 10))) diag(c, D_IRQ_LAPIC, ST_UNSUPPORTED, "APIC disabled or x2APIC mode");
        else {
            u64 base = ab & 0x000ffffffffff000ull;
            static const u16 regs[31] = { 0x20, 0x30, 0x80, 0x90, 0xa0, 0xd0, 0xe0, 0xf0,
                0x100, 0x110, 0x120, 0x130, 0x140, 0x150, 0x160, 0x170,
                0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270,
                0x320, 0x330, 0x340, 0x350, 0x360, 0x370, 0x3e0 };
            if (!prb_mmio_ok(c, base, 0x1000)) diag(c, D_IRQ_LAPIC, ST_SKIPPED_SAFETY, "LAPIC page not mapped UC");
            else {
                u32 v[62];
                for (u32 i = 0; i < 31; i++) { v[2 * i] = regs[i]; v[2 * i + 1] = hal_mmio32(c, base + regs[i]); }
                rec(c, T_MMIO32, base, RG_LAPIC, v, sizeof v);           /* (offset, value) pairs */
                diag(c, D_IRQ_LAPIC, ST_OK, "LAPIC id/version/TPR/ISR/IRR/LVT (no EOI, no writes)");
            }
        }
    }
    diag(c, D_IRQ_MSI, c->ecam_ok ? ST_OK : ST_UNSUPPORTED,
         c->ecam_ok ? "MSI/MSI-X capabilities decoded host-side from T_PCICFG; glue MSI window in D_SB_REGS"
                    : "no PCI scan");
    diag(c, D_IRQ_IOAPIC, c->dmap ? ST_OK : ST_UNSUPPORTED,
         c->dmap ? "IOAPIC entries decoded host-side from the ACPI MADT" : "no ACPI access");
}
