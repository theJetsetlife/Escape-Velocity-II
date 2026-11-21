/* PS4 Pro (CUH-70xx/71xx/72xx). GPU id: ps4_bridge.c PCI_DEVICE_ID_CUH_7XXX 0x9924. No available
 * source pairs it with a southbridge; its display block is not assumed to equal Liverpool's. */
#include "../detection/platform.h"

const struct board_profile board_pro = {
    .name = "PS4 Pro CUH-7xxx (GPU 0x9924)", .board = BRD_PRO, .state = VS_UNTESTED, .fb_level = LVL_DOCUMENTED,
    .gpu_device = 0x9924, .sb_documented = { 0, 0, 0, 0 },
};
