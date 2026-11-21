/* ring0.c - entered through kexec (syscall 11 -> fn(td, uap), uap[1] = ctx). One phase per call so
 * the userland half flushes evidence to the PC between phases. READ-ONLY: the only stores go to
 * the caller's mlock'd record buffer and context. No libc, no libPS4 calls in ring0. */
#include "../diagnostics/diag.h"
#include "../firmware/fw.h"
#include "../platform/detection/platform.h"

int prb_ring0(void *td, u64 *uap)
{
    (void)td;
    struct prb_ctx *c = (struct prb_ctx *)uap[1];
    if (!c || c->magic != PRB_CTX_MAGIC) return 0;
    u64 fl = hal_irq_save();
    c->id.ring0 = 1;
    switch (c->phase) {
    case PH_FW:   fw_kernel_identify(c); fw_dmap_prove(c); break;
    case PH_CPU:  dx_cpu(c); dx_mtrr(c); pci_ecam_setup(c); dx_tsc(c, 0); break;
    case PH_TSC:  dx_tsc(c, 1); break;
    case PH_PCI:  dx_pci_scan(c); break;
    case PH_SB:   dx_sb_regs(c); break;
    case PH_IRQ:  dx_irq(c); break;
    case PH_GPU:  dx_gpu_fb(c); break;
    case PH_ACPI: dx_acpi(c); break;
    case PH_DEV:  dx_usb(c); dx_storage(c); dx_net(c); dx_hpet(c); break;
    case PH_MEM:
        if (c->dmap) dx_dmap_runs(c);
        else diag(c, D_MEM_DMAP, ST_UNSUPPORTED, "no proven direct map");
        break;
    }
    c->id.profile_state = (u8)plat_profile_state(&c->id);
    c->ok |= 1u << c->phase;
    hal_irq_restore(fl);
    return 0;
}
