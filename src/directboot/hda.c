/* hda.c - see hda.h. */
#include "hda.h"

#define GCAP     0x00
#define GCTL     0x08
#define INTCTL   0x20
#define INTSTS   0x24
#define CORBCTL  0x4c
#define RIRBCTL  0x5c
#define DPLBASE  0x70
#define DPUBASE  0x74
#define SD_BASE  0x80      /* stream n: 0x80 + 0x20 * n (SDI0..., then SDO..., then BSS) */
#define SD_STRIDE 0x20

#define GCTL_RESET     (1u << 0)
#define GCTL_UNSOL     (1u << 8)
#define CORBCTL_RUN    (1u << 1)
#define RBCTL_DMA_EN   (1u << 1)
#define SD_CTL_RUN     0x02u   /* SD_CTL_DMA_START */
#define SD_INT_MASK    0x1cu   /* DESC_ERR | FIFO_ERR | COMPLETE */

static u32 rd32(volatile u8 *m, u32 o) { return *(volatile u32 *)(m + o); }
static void wr32(volatile u8 *m, u32 o, u32 v) { *(volatile u32 *)(m + o) = v; }
static u8 rd8(volatile u8 *m, u32 o) { return *(volatile u8 *)(m + o); }
static void wr8(volatile u8 *m, u32 o, u8 v) { *(volatile u8 *)(m + o) = v; }

int hda_stop(volatile u8 *mmio, struct hda_stop_result *r, void (*delay_us)(void *ctx, u32 us), void *ctx)
{
    r->gcap = rd32(mmio, GCAP) & 0xffff;
    r->gctl_before = rd32(mmio, GCTL);
    if (r->gcap == 0xffff || r->gctl_before == 0xffffffffu) return -1;      /* nothing decodes here */
    u32 iss = (r->gcap >> 8) & 15, oss = (r->gcap >> 12) & 15, bss = (r->gcap >> 3) & 31;
    r->streams = iss + oss + bss;
    if (r->streams > 30) r->streams = 30;
    r->corb_rirb_before = rd8(mmio, CORBCTL) | (u32)rd8(mmio, RIRBCTL) << 8;
    r->dplbase_before = rd32(mmio, DPLBASE);

    /* 1. interrupts off (azx_int_disable): INTCTL = 0 clears GIE/CIE and every stream SIE */
    wr32(mmio, INTCTL, 0);

    /* 2. every stream: clear RUN and its interrupt enables (snd_hdac_stream_clear, dword access
     *    as for access_sdnctl_in_dword controllers), then wait for RUN to read back 0 */
    r->running_before = 0;
    for (u32 n = 0; n < r->streams; n++) {
        u32 sd = SD_BASE + SD_STRIDE * n;
        u32 ctl = rd32(mmio, sd);
        if (ctl & SD_CTL_RUN) r->running_before++;
        wr32(mmio, sd, ctl & ~(SD_CTL_RUN | SD_INT_MASK) & 0x00ffffffu);   /* byte 3 = SD_STS: write 0 */
        wr8(mmio, sd + 3, (u8)SD_INT_MASK);                                    /* clear status, "to be sure" */
    }
    for (u32 t = 0; t < 100; t++) {
        u32 busy = 0;
        for (u32 n = 0; n < r->streams; n++) busy |= rd32(mmio, SD_BASE + SD_STRIDE * n) & SD_CTL_RUN;
        if (!busy) break;
        delay_us(ctx, 100);
    }

    /* 3. command rings off (snd_hdac_bus_stop_cmd_io), bounded wait as hdac_wait_for_cmd_dmas */
    wr8(mmio, RIRBCTL, 0);
    wr8(mmio, CORBCTL, 0);
    for (u32 t = 0; t < 1000 && (rd8(mmio, RIRBCTL) & RBCTL_DMA_EN); t++) delay_us(ctx, 100);
    for (u32 t = 0; t < 1000 && (rd8(mmio, CORBCTL) & CORBCTL_RUN); t++) delay_us(ctx, 100);
    wr32(mmio, GCTL, rd32(mmio, GCTL) & ~GCTL_UNSOL);

    /* 4. position buffer off (bit 0 of DPLBASE is the enable) */
    wr32(mmio, DPLBASE, 0);
    wr32(mmio, DPUBASE, 0);

    /* 5. controller reset (snd_hdac_bus_enter_link_reset): CRST = 0, wait up to 100 ms */
    wr32(mmio, GCTL, rd32(mmio, GCTL) & ~GCTL_RESET);
    for (u32 t = 0; t < 1000 && (rd32(mmio, GCTL) & GCTL_RESET); t++) delay_us(ctx, 100);
    wr32(mmio, INTSTS, 0xffffffffu);   /* RW1C: drop anything latched */

    r->running_after = 0;
    for (u32 n = 0; n < r->streams; n++)
        if (rd32(mmio, SD_BASE + SD_STRIDE * n) & SD_CTL_RUN) r->running_after++;
    r->corb_rirb_after = rd8(mmio, CORBCTL) | (u32)rd8(mmio, RIRBCTL) << 8;
    r->gctl_after = rd32(mmio, GCTL);
    return (r->running_after || (r->corb_rirb_after & 0x0202) || (r->gctl_after & GCTL_RESET)) ? 1 : 0;
}
