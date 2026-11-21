/* GoldHEN BinLoader. Evidence: OpenGoldHEN src/kpayload/patches.c installs sysent[11] = kexec_rop with
 * 2 args unconditionally; on 9.00 kexec_rop (ksdk_offsets_900 IMP_KEXEC_ROP = 0x4c7ad) is the `jmp [rsi]`
 * gadget (ff 26, checked against the retail 9.00 kernel dump). Versions = OpenGoldHEN
 * assets/tables/goldhen_offsets_<ver>.bin, plus 14.00 from an unofficial experimental GoldHEN BD-J build
 * (firmware/profiles/sources/README.md). Not run with this probe: experimental. */
#include "../adapters.h"

const struct prb_adapter adapter_goldhen = {
    .name = "GoldHEN", .env = ENV_GOLDHEN, .state = VS_EXPERIMENTAL, .loader_port = 9090, .kexec_any = 11,
    .nver = 29,
    .ver = { 505, 671, 672, 702, 750, 751, 755, 800, 801, 803, 850, 852, 900, 903, 904, 950, 951, 960,
             1000, 1001, 1050, 1071, 1100, 1102, 1152, 1202, 1252, 1300, 1400 },
    .kexec_nr = { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
                  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11 },
};
