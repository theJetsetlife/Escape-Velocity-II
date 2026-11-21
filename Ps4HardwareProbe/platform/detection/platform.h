/* platform.h - console family from hardware identities, never from marketing names.
 * Adding a board revision = one more board_profile; the core is unchanged. */
#ifndef PRB_PLATFORM_H
#define PRB_PLATFORM_H

#include "../../core/probe.h"

struct board_profile {
    char name[40];
    u8   board;                  /* enum prb_board */
    u8   state;                  /* enum prb_vstate for this probe */
    u8   fb_level;               /* read level for the display (CRTC) registers of this GPU */
    u8   rsvd;
    u16  gpu_device;             /* vendor 0x1002 */
    u8   sb_documented[4];       /* southbridge families a source pairs with this GPU (0 = none) */
};

u32 board_count(void);
const struct board_profile *board_at(u32 i);
const struct board_profile *board_by_gpu(u16 gpu_device);

/* Pure function over the scanned bus: fills gpu_*, sb_*, glue_*, board, board_state, sb_state.
 * ST_OK when GPU and southbridge are both recognised, ST_UNSUPPORTED otherwise. */
u32 plat_identify(const struct prb_pcidev *d, u32 n, struct prb_ident *id);

/* State of the selected compatibility profile = the weakest of board, southbridge and firmware. */
u32 plat_profile_state(const struct prb_ident *id);

#endif
