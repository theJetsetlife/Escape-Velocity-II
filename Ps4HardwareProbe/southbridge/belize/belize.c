/* Belize. PS4 Linux binds 0x90cc to the Aeolia driver (sb-aeolia.c, aeolia_family), so it reads the
 * same chip id (BAR2+0x1104..) and RTC (BAR2+0x130) at every boot on Belize consoles. The glue windows
 * are only used by that driver together with writes, so they stay at level 2 until read on a Belize. */
#include "../sb.h"

const struct sb_backend sb_belize = {
    .name = "Belize", .family = SBF_BELIZE, .state = VS_EXPERIMENTAL, .chipid_bar = 0x18, .chipid_level = LVL_HWPROVEN,
    .ids = { 0x90c8, 0x90c9, 0x90ca, 0x90cb, 0x90cc, 0x90cd, 0x90ce, 0x90cf },
    .chipid_off = { 0x1104, 0x1108, 0x110c },
    .nwin = 3,
    .win = {
        { 0x18, LVL_HWPROVEN,   RG_SB_RTC,    0, 0x130,    0x10  },
        { 0x20, LVL_DOCUMENTED, RG_SB_BARWIN, 0, 0x1c8000, 0x200 },
        { 0x20, LVL_DOCUMENTED, RG_SB_MSI,    0, 0x1c8400, 0x200 },
    },
};
