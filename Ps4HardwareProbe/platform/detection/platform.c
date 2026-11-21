/* platform.c - identify GPU, southbridge and board family from the scanned PCI functions. */
#include "platform.h"
#include "../../southbridge/sb.h"

extern const struct board_profile board_fat_11, board_fat_12, board_slim, board_pro;

u32 board_count(void) { return 4; }

const struct board_profile *board_at(u32 i)
{
    switch (i) { case 0: return &board_fat_11; case 1: return &board_fat_12; case 2: return &board_slim;
                 case 3: return &board_pro; }
    return 0;
}

const struct board_profile *board_by_gpu(u16 gpu)
{
    for (u32 i = 0; i < board_count(); i++) if (board_at(i)->gpu_device == gpu) return board_at(i);
    return 0;
}

u32 plat_identify(const struct prb_pcidev *d, u32 n, struct prb_ident *id)
{
    u32 votes[SBF_UNKNOWN_SONY + 1] = { 0 };
    id->gpu_found = 0; id->sb_family = SBF_NONE; id->sb_funcs = 0; id->board = BRD_UNKNOWN;
    id->board_state = VS_UNTESTED; id->sb_state = VS_UNTESTED;

    for (u32 i = 0; i < n; i++) {
        /* GPU: AMD, display class 0x03 */
        if (d[i].vendor == 0x1002 && (d[i].class_rev >> 24) == 0x03 && !id->gpu_found) {
            id->gpu_found = 1; id->gpu_device = d[i].device;
        }
        if (d[i].vendor != 0x104d) continue;
        u32 fn; const struct sb_backend *b = sb_by_device(d[i].device, &fn);
        if (!b) { votes[SBF_UNKNOWN_SONY]++; continue; }
        votes[b->family]++;
        if (fn == SBFN_GLUE) {
            id->sb_glue_device = d[i].device;
            id->glue_bus = d[i].bus; id->glue_dev = d[i].dev; id->glue_fn = d[i].fn;
        }
    }
    /* the chip is the family of the glue function; the other functions must agree */
    const struct sb_backend *g = id->sb_glue_device ? sb_by_device(id->sb_glue_device, 0) : 0;
    if (g) {
        id->sb_family = g->family; id->sb_funcs = (u8)votes[g->family]; id->sb_state = g->state;
        for (u32 f = SBF_NONE + 1; f <= SBF_UNKNOWN_SONY; f++)
            if (f != g->family && votes[f]) { id->sb_family = SBF_UNKNOWN_SONY; id->sb_state = VS_UNTESTED; }
    } else {
        u32 any = 0;
        for (u32 f = SBF_NONE + 1; f <= SBF_UNKNOWN_SONY; f++) any += votes[f];
        if (any) id->sb_family = SBF_UNKNOWN_SONY;        /* Sony functions but no known glue */
    }

    const struct board_profile *bp = id->gpu_found ? board_by_gpu(id->gpu_device) : 0;
    if (bp) {
        id->board = bp->board; id->board_state = bp->state;
        int documented = 0;
        for (u32 k = 0; k < 4; k++) if (bp->sb_documented[k] && bp->sb_documented[k] == id->sb_family) documented = 1;
        if (!documented && id->board_state > VS_EXPERIMENTAL) id->board_state = VS_EXPERIMENTAL;
    }
    return (bp && g && id->sb_family != SBF_UNKNOWN_SONY) ? ST_OK : ST_UNSUPPORTED;
}

u32 plat_profile_state(const struct prb_ident *id)
{
    u32 fw = id->fw_state == FWS_VERIFIED ? VS_VERIFIED : id->fw_state == FWS_VALIDATED ? VS_EXPERIMENTAL : VS_UNTESTED;   /* IDENTIFIED: no memory profile */
    u32 s = id->board_state;
    if (id->sb_state < s) s = id->sb_state;
    if (fw < s) s = fw;
    if (id->board == BRD_UNKNOWN || id->sb_family == SBF_NONE || id->sb_family == SBF_UNKNOWN_SONY) s = VS_UNTESTED;
    return s;
}
