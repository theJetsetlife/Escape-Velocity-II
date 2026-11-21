/* WebKitty Bin Loader. Two kernel chains with different kexec syscalls (WebKitty sources):
 *   psfree-lapse 7.00-9.60 (src/psfree-lapse/lapse/ps4/<ver>.mjs version comments): kpatch/<ver>.c writes
 *     sysent[11] = { narg 2, jmp_rsi gadget } -> kexec = syscall 11. 9.00 ran ring0 this way (src/diag).
 *   relapse 11.00-13.52 (src/relapse/ps4_offsets.js, incl. alias_of entries): jb.js overwrites
 *     sysent[661] (k_sysent_661) with k_jmp_rsi -> kexec = syscall 661.
 * Other versions: no kexec known, no ring0. */
#include "../adapters.h"

const struct prb_adapter adapter_webkitty = {
    .name = "WebKitty", .env = ENV_WEBKITTY, .state = VS_PARTIAL, .loader_port = 9020, .kexec_any = 0,
    .nver = 28,
    .ver = { 700, 701, 702, 750, 751, 755, 800, 801, 803, 850, 852, 900, 903, 904, 950, 951, 960,
             1100, 1150, 1200, 1202, 1250, 1252, 1300, 1302, 1304, 1350, 1352 },
    .kexec_nr = { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
                  661, 661, 661, 661, 661, 661, 661, 661, 661, 661, 661 },
};

extern const struct prb_adapter adapter_goldhen;

const struct prb_adapter *adapter_for(u32 env)
{
    if (env == ENV_WEBKITTY) return &adapter_webkitty;
    if (env == ENV_GOLDHEN) return &adapter_goldhen;
    return 0;
}

u32 adapter_kexec_nr(const struct prb_adapter *a, u16 ver)
{
    if (!a) return 0;
    for (u32 i = 0; i < a->nver && i < PRB_ADAPTER_MAXVER; i++) if (a->ver[i] == ver) return a->kexec_nr[i];
    return a->kexec_any;
}
