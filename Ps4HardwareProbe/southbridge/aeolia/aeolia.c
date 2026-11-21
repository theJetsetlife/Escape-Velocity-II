/* Aeolia. Sources: PS4 Linux drivers/ps4/ps4sb.h + sb-aeolia.c: aeolia_glue_probe() reads chip id
 * BAR2+0x1104/0x1108/0x110c at every boot; ps4sb_rtc_init() reads the RTC (BAR2+0x130, byte regs).
 * Glue BAR windows and MSI control were read on a PS4 Fat 9.00 by src/diag (run 2).
 * Never read: ICC (BAR4+0x184000) and SPM (protocol buffers). */
#include "../sb.h"

const struct sb_backend sb_aeolia = {
    .name = "Aeolia", .family = SBF_AEOLIA, .state = VS_PARTIAL, .chipid_bar = 0x18, .chipid_level = LVL_HWPROVEN,
    .ids = { 0x908f, 0x909e, 0x909f, 0x90a0, 0x90a1, 0x90a2, 0x90a3, 0x90a4 },
    .chipid_off = { 0x1104, 0x1108, 0x110c },
    .nwin = 3,
    .win = {
        { 0x18, LVL_HWPROVEN,   RG_SB_RTC,    0, 0x130,    0x10  },   /* RTC counter (Linux, every boot) */
        { 0x20, LVL_HWPROVEN,   RG_SB_BARWIN, 0, 0x1c8000, 0x200 },   /* glue BAR windows (diag run 2)   */
        { 0x20, LVL_HWPROVEN,   RG_SB_MSI,    0, 0x1c8400, 0x200 },   /* glue MSI control (diag run 2)   */
    },
};
