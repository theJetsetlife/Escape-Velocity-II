/* diag.h - diagnostics. Each ends with exactly one T_DIAG (OK / UNSUPPORTED / SKIPPED_SAFETY /
 * FAILED + reason). Shared core; hardware differences live in data (southbridge/, platform/). */
#ifndef PRB_DIAG_H
#define PRB_DIAG_H

#include "../core/hal.h"
#include "../logging/rec.h"

void dx_cpu(struct prb_ctx *c);              /* cpu/      D_CPU_ID D_CPU_TOPOLOGY D_CPU_CREGS D_CPU_MSR */
void dx_mtrr(struct prb_ctx *c);             /* memory/   D_MEM_MTRR */
void dx_dmap_runs(struct prb_ctx *c);        /* memory/   D_MEM_DMAP */
void pci_ecam_setup(struct prb_ctx *c);      /* pci/      D_PCI_ECAM */
void dx_pci_scan(struct prb_ctx *c);         /* pci/      D_PCI_SCAN D_PLATFORM D_SB_IDENT D_GPU_ID */
void dx_net(struct prb_ctx *c);              /* pci/      D_NET */
void dx_sb_regs(struct prb_ctx *c);          /* southbridge/sbregs.c  D_SB_REGS */
void dx_irq(struct prb_ctx *c);              /* interrupts/ D_IRQ_LAPIC D_IRQ_MSI D_IRQ_IOAPIC */
void dx_gpu_fb(struct prb_ctx *c);           /* gpu/      D_GPU_FB */
void dx_acpi(struct prb_ctx *c);             /* acpi/     D_ACPI */
void dx_usb(struct prb_ctx *c);              /* usb/      D_USB */
void dx_storage(struct prb_ctx *c);          /* storage/  D_SATA D_SDHCI */
void dx_tsc(struct prb_ctx *c, u32 sample);  /* timers/   D_TSC (sample 1) */
void dx_hpet(struct prb_ctx *c);             /* timers/   D_HPET */

enum { DC_OTHER, DC_XHCI, DC_EHCI, DC_AHCI, DC_SDHCI, DC_NET };
u32  dev_class(const struct prb_pcidev *d, int *sony);
void dev_caps(struct prb_ctx *c, const struct prb_pcidev *d, u32 bar_off, u32 off, u32 len);

#endif
