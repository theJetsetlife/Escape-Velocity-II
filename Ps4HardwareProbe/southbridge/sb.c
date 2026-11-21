/* sb.c - backend lookup. Identification is by PCI device id of the functions actually present. */
#include "sb.h"

extern const struct sb_backend sb_aeolia, sb_belize, sb_baikal;

u32 sb_count(void) { return 3; }

const struct sb_backend *sb_at(u32 i)
{
    /* a switch, not a pointer table: a static array of pointers would need relocations */
    switch (i) { case 0: return &sb_aeolia; case 1: return &sb_belize; case 2: return &sb_baikal; }
    return 0;
}

const struct sb_backend *sb_by_family(u32 family)
{
    for (u32 i = 0; i < sb_count(); i++) if (sb_at(i)->family == family) return sb_at(i);
    return 0;
}

const struct sb_backend *sb_by_device(u16 device, u32 *func)
{
    for (u32 i = 0; i < sb_count(); i++) {
        const struct sb_backend *b = sb_at(i);
        for (u32 f = 0; f < SBFN_COUNT; f++)
            if (b->ids[f] == device) { if (func) *func = f; return b; }
    }
    return 0;
}
