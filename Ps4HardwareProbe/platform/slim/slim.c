/* PS4 Slim (CUH-20xx/21xx/22xx). GPU id: ps4_bridge.c PCI_DEVICE_ID_CUH_2XXX 0x9923 (amdgpu
 * liverpool_clk.c treats 0x9923 as "slim"). No available source pairs it with a southbridge: the probe
 * reports what it finds (Belize or Baikal backends are selected by device id). */
#include "../detection/platform.h"

const struct board_profile board_slim = {
    .name = "PS4 Slim CUH-2xxx (GPU 0x9923)", .board = BRD_SLIM, .state = VS_UNTESTED, .fb_level = LVL_DOCUMENTED,
    .gpu_device = 0x9923, .sb_documented = { 0, 0, 0, 0 },
};
