/* Baikal. PS4 Linux ps4sb.h + sb-baikal.c: baikal_glue_probe() reads chip id BAR4+0xC020/0xC024 and
 * revision BAR4+0x4084 at every boot; baikal_family RTC = BAR4+0x50d0 (status 0x50cc), read by
 * ps4sb_rtc_init(). HPET (BAR4+0x109000) is used by the driver as a timer: level 2.
 * Never read: ICC (BAR2+0x108000-0x800) and SPM. */
#include "../sb.h"

const struct sb_backend sb_baikal = {
    .name = "Baikal", .family = SBF_BAIKAL, .state = VS_EXPERIMENTAL, .chipid_bar = 0x20, .chipid_level = LVL_HWPROVEN,
    .ids = { 0x90d7, 0x90d8, 0x90d9, 0x90da, 0x90db, 0x90dc, 0x90dd, 0x90de },
    .chipid_off = { 0xc020, 0xc024, 0x4084 },
    .nwin = 2,
    .win = {
        { 0x20, LVL_HWPROVEN,   RG_SB_RTC,    0, 0x50cc,   0x8  },    /* RTC status + counter */
        { 0x20, LVL_DOCUMENTED, RG_HPET,      0, 0x109000, 0x10 },    /* HPET caps            */
    },
};
