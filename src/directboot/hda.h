/* hda.h - stop an Intel-HDA-compatible audio controller through its own registers, the way
 * Linux does (sound/hda/core/controller.c snd_hdac_bus_stop_chip + stream.c snd_hdac_stream_clear +
 * snd_hdac_bus_enter_link_reset). Used by the stage on the GPU's HDMI audio function 00:01.1
 * (HW run 06:53 UTC: froze mid-print at the same CpuDxe line as 23:53/00:30/01:50 while HDMI audio
 * kept streaming; with the IOMMU off its DMA lands on raw physical addresses), and by rebuild/qemu
 * against QEMU's intel-hda so the sequence runs off the console first.
 * Offsets: strawberry include/sound/hda_register.h. Shared by the freestanding stage and the harness. */
#ifndef DB_HDA_H
#define DB_HDA_H
#include "../ps4.h"

struct hda_stop_result {
    u32 gcap;              /* GCAP as read */
    u32 streams;           /* ISS + OSS + BSS */
    u32 running_before;    /* streams with SD_CTL.RUN set before */
    u32 running_after;     /* streams still running after (should be 0) */
    u32 corb_rirb_before;  /* CORBCTL | RIRBCTL << 8 before */
    u32 corb_rirb_after;
    u32 dplbase_before;
    u32 gctl_before, gctl_after;   /* GCTL.CRST (bit 0) must be 0 after */
};

/* mmio = CPU-visible BAR0 mapping; delay_us(ctx, us) must wait at least us microseconds.
 * Every wait is bounded (<= ~100 ms each). Returns 0 when the controller ended in reset with
 * no stream, CORB or RIRB DMA running, nonzero otherwise (still safe: best effort). */
int hda_stop(volatile u8 *mmio, struct hda_stop_result *r, void (*delay_us)(void *ctx, u32 us), void *ctx);

#endif
