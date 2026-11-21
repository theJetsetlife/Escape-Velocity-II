/* gpu.c - framebuffer metadata from the display controller (no pixel reads).
 * Register map: DCE 8 / CIK offsets (amdgpu dce_8_0_d.h, cikd.h, gmc_7_1_d.h), read on a PS4 Fat
 * (GPU 0x9920) by src/diag run 2. The level each GPU needs is part of its board profile
 * (platform/<family>/ fb_level); an unknown GPU id gets no display register reads. */
#include "../diag.h"
#include "../../southbridge/sb.h"
#include "../../platform/detection/platform.h"

void dx_gpu_fb(struct prb_ctx *c)
{
    if (!c->ecam_ok || !c->id.gpu_found) { diag(c, D_GPU_FB, ST_UNSUPPORTED, "no GPU identified"); return; }
    const struct board_profile *bp = board_by_gpu(c->id.gpu_device);
    if (!bp) { diag(c, D_GPU_FB, ST_SKIPPED_SAFETY, "GPU id has no board profile: display register map unknown"); return; }
    if (c->cfg.level < bp->fb_level) { diag(c, D_GPU_FB, ST_SKIPPED_SAFETY, "this GPU's display registers need a higher read level"); return; }
    const struct prb_pcidev *g = 0;
    for (u32 i = 0; i < c->ndev && !g; i++) if (c->dev[i].vendor == 0x1002 && (c->dev[i].class_rev >> 24) == 3) g = &c->dev[i];
    u64 mm = prb_bar(c, g->bus, g->dev, g->fn, 0x24);
    if (!mm) { diag(c, D_GPU_FB, ST_FAILED, "GPU BAR5 empty"); return; }
    if (!prb_mmio_ok(c, mm, 0x13000)) { diag(c, D_GPU_FB, ST_SKIPPED_SAFETY, "GPU registers not mapped UC"); return; }
    static const u16 gmc[5] = { 0x809, 0x80c, 0x80d, 0x80e, 0x81a };
    static const u16 crtc[6] = { 0x0, 0x300, 0x2600, 0x2900, 0x2c00, 0x2f00 };
    static const u16 grph[8] = { 0x1a00, 0x1a01, 0x1a04, 0x1a06, 0x1a07, 0x1a0d, 0x1a0e, 0x1b9c };
    u32 v[8];
    for (u32 i = 0; i < 5; i++) v[i] = hal_mmio32(c, mm + gmc[i] * 4u);
    rec(c, T_MMIO32, mm, RG_GPU_GMC, v, 20);
    u32 on = 0;
    for (u32 n = 0; n < 6; n++) {
        for (u32 i = 0; i < 8; i++) v[i] = hal_mmio32(c, mm + (u64)(grph[i] + crtc[n]) * 4);
        rec(c, T_MMIO32, mm, RG_GPU_CRTC | n << 8, v, sizeof v);   /* ENABLE CTRL SURF_LO PITCH SURF_HI X_END Y_END */
        on += v[0] & 1;
    }
    rec_s(c, T_NOTE, on, 0, "active CRTCs");
    diag(c, D_GPU_FB, ST_OK, "scanout surface registers of 6 CRTCs");
}
