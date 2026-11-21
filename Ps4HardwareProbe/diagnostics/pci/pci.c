/* pci.c - ECAM discovery from the CPU (MMIO_CFG_BASE), bus scan (bus 0 + buses behind bridges on
 * bus 0, bounded by the ECAM bus range and each bus's mapping/UC check), identification, network
 * discovery and the shared device classifier. Config space is only ever read. */
#include "../diag.h"
#include "../../platform/detection/platform.h"
#include "../../southbridge/sb.h"

void pci_ecam_setup(struct prb_ctx *c)
{
    c->ecam_ok = 0;
    if (!c->fam16h) { diag(c, D_PCI_ECAM, ST_SKIPPED_SAFETY, "MMIO_CFG_BASE is a family-16h MSR"); return; }
    if (!c->dmap) { diag(c, D_PCI_ECAM, ST_UNSUPPORTED, "no proven direct map"); return; }
    u64 mc = hal_rdmsr(0xc0010058);
    c->ecam = mc & 0x0000fffffff00000ull;
    u32 range = (u32)(mc >> 2) & 0xf;
    c->ecam_buses = range <= 8 ? 1u << range : 1;
    if (!(mc & 1) || !c->ecam) { diag(c, D_PCI_ECAM, ST_UNSUPPORTED, "ECAM disabled in MMIO_CFG_BASE"); return; }
    if (!prb_mmio_ok(c, c->ecam, 1u << 20)) { diag(c, D_PCI_ECAM, ST_SKIPPED_SAFETY, "bus 0 ECAM not mapped UC"); return; }
    c->ecam_ok = 1;
    diag(c, D_PCI_ECAM, ST_OK, "ECAM from MMIO_CFG_BASE");
}

static void scan_bus(struct prb_ctx *c, u32 bus)
{
    for (u32 dev = 0; dev < 32; dev++) {
        u32 nfn = 1;
        for (u32 fn = 0; fn < nfn; fn++) {
            u32 id = prb_cfg32(c, bus, dev, fn, 0);
            u32 hdr = (prb_cfg32(c, bus, dev, fn, 0xc) >> 16) & 0xff;
            if (fn == 0 && id != 0xffffffffu && (hdr & 0x80)) nfn = 8;
            if (id == 0xffffffffu || id == 0) continue;
            u32 *d = rec_begin(c, T_PCICFG, bus << 16 | dev << 8 | fn, 0, 256);
            if (d) for (u32 o = 0; o < 256; o += 4) d[o / 4] = prb_cfg32(c, bus, dev, fn, o);
            if (c->ndev < PRB_MAXDEV) {
                struct prb_pcidev *p = &c->dev[c->ndev++];
                p->bus = (u8)bus; p->dev = (u8)dev; p->fn = (u8)fn; p->hdr = (u8)hdr;
                p->vendor = (u16)id; p->device = (u16)(id >> 16);
                p->class_rev = prb_cfg32(c, bus, dev, fn, 8);
            }
        }
    }
}

void dx_pci_scan(struct prb_ctx *c)
{
    if (!c->ecam_ok) {
        diag(c, D_PCI_SCAN, ST_UNSUPPORTED, "no usable ECAM");
        diag(c, D_PLATFORM, ST_UNSUPPORTED, "no PCI scan: console family from CPUID only");
        diag(c, D_SB_IDENT, ST_UNSUPPORTED, "no PCI scan");
        diag(c, D_GPU_ID, ST_UNSUPPORTED, "no PCI scan");
        return;
    }
    c->ndev = 0;
    scan_bus(c, 0);
    u32 n0 = c->ndev, extra = 0;
    for (u32 i = 0; i < n0; i++) {
        if ((c->dev[i].hdr & 0x7f) != 1) continue;                   /* PCI-PCI bridge */
        u32 bn = prb_cfg32(c, 0, c->dev[i].dev, c->dev[i].fn, 0x18);
        u32 sec = (bn >> 8) & 0xff, sub = (bn >> 16) & 0xff;
        for (u32 b = sec; b && b <= sub && b < c->ecam_buses && extra < 8; b++, extra++) {
            if (!prb_mmio_ok(c, c->ecam + ((u64)b << 20), 1u << 20)) continue;
            scan_bus(c, b);
        }
    }
    rec_s(c, T_NOTE, c->ndev, extra, "pci functions | secondary buses scanned");
    diag(c, D_PCI_SCAN, c->ndev ? ST_OK : ST_FAILED, c->ndev ? "bus scan complete" : "no functions found");

    u32 st = plat_identify(c->dev, c->ndev, &c->id);
    diag(c, D_PLATFORM, st, st == ST_OK ? "GPU and southbridge recognised"
                                        : "unrecognised hardware combination: limited discovery");
    diag(c, D_GPU_ID, c->id.gpu_found ? ST_OK : ST_UNSUPPORTED,
         c->id.gpu_found ? (board_by_gpu(c->id.gpu_device) ? "known PS4 GPU id" : "AMD GPU with unknown id")
                         : "no AMD display function");
    diag(c, D_SB_IDENT, (c->id.sb_family == SBF_NONE || c->id.sb_family == SBF_UNKNOWN_SONY) ? ST_UNSUPPORTED : ST_OK,
         c->id.sb_family == SBF_NONE ? "no Sony functions" :
         c->id.sb_family == SBF_UNKNOWN_SONY ? "Sony functions of unknown/mixed generation: no backend" :
         "southbridge recognised by device ids");
}

u32 dev_class(const struct prb_pcidev *d, int *sony)
{
    *sony = 0;
    if (d->vendor == 0x104d) {
        u32 fn; const struct sb_backend *b = sb_by_device(d->device, &fn);
        if (b) {
            *sony = 1;
            if (fn == SBFN_XHCI) return DC_XHCI;
            if (fn == SBFN_AHCI) return DC_AHCI;
            if (fn == SBFN_SDHCI) return DC_SDHCI;
            if (fn == SBFN_GBE) return DC_NET;
        }
        return DC_OTHER;
    }
    u32 cls = d->class_rev >> 8;
    if (cls == 0x0c0330) return DC_XHCI;
    if (cls == 0x0c0320) return DC_EHCI;
    if (cls == 0x010601) return DC_AHCI;
    if ((cls >> 8) == 0x0805) return DC_SDHCI;
    if ((cls >> 16) == 0x02) return DC_NET;
    return DC_OTHER;
}

void dev_caps(struct prb_ctx *c, const struct prb_pcidev *d, u32 bar_off, u32 off, u32 len)
{
    u64 bar = prb_bar(c, d->bus, d->dev, d->fn, bar_off);
    if (!bar) { rec_s(c, T_NOTE, (u64)d->bus << 16 | d->dev << 8 | d->fn, bar_off, "no memory BAR"); return; }
    prb_dump_mmio(c, bar + off, len, RG_DEV_CAP | (u64)(d->bus << 16 | d->dev << 8 | d->fn) << 8);
}

void dx_net(struct prb_ctx *c)
{
    if (!c->ecam_ok) { diag(c, D_NET, ST_UNSUPPORTED, "no PCI scan"); return; }
    u32 n = 0; int sony;
    for (u32 i = 0; i < c->ndev; i++) if (dev_class(&c->dev[i], &sony) == DC_NET) n++;
    rec_s(c, T_NOTE, n, 0, "network controllers (identification only; no register reads)");
    diag(c, D_NET, n ? ST_OK : ST_UNSUPPORTED, n ? "network controller(s) identified" : "no network controller found");
}
