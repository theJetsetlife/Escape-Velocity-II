/* sb.h - southbridge backends. Data only (no function pointers: the blob has no relocator).
 * A backend says how to recognise a chip (PCI ids), how to read its identity (chip id + revision
 * registers, which the chip's PS4 Linux driver reads at every boot) and which further register
 * windows may be read, each tagged with the read level it needs and the evidence for it.
 * Adding a southbridge generation = one more data file here; the core is unchanged. */
#ifndef PRB_SB_H
#define PRB_SB_H

#include "../core/probe.h"

enum sb_func { SBFN_ACPI, SBFN_GBE, SBFN_AHCI, SBFN_SDHCI, SBFN_GLUE, SBFN_DMAC, SBFN_MEM, SBFN_XHCI, SBFN_COUNT };

struct sb_window {
    u8  bar;          /* glue config offset of the BAR (0x10 + 4*n) */
    u8  level;        /* enum prb_level needed */
    u8  region;       /* region id in T_MMIO32.b */
    u8  rsvd;
    u32 off, len;
};

struct sb_backend {
    char name[8];
    u8   family;                 /* enum prb_sbf */
    u8   state;                  /* enum prb_vstate */
    u8   chipid_bar;             /* glue BAR holding the identity registers */
    u8   chipid_level;           /* level for the identity read */
    u16  ids[SBFN_COUNT];        /* PCI device ids, vendor 0x104d (PS4 Linux pci_ids.h) */
    u32  chipid_off[3];          /* chip id 0, chip id 1, revision (BAR offsets) */
    u32  nwin;
    struct sb_window win[4];
};

/* region ids (T_MMIO32.b) */
enum { RG_SB_BARWIN = 1, RG_SB_MSI = 2, RG_SB_CHIPID = 3, RG_SB_RTC = 4,
       RG_GPU_GMC = 6, RG_GPU_CRTC = 7, RG_LAPIC = 8, RG_HPET = 9, RG_DEV_CAP = 10 };

const struct sb_backend *sb_by_family(u32 family);
const struct sb_backend *sb_by_device(u16 device, u32 *func);
u32 sb_count(void);
const struct sb_backend *sb_at(u32 i);

#endif
