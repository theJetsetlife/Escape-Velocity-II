/* PS4 original ("Fat"). GPU ids: PS4 Linux drivers/gpu/drm/{radeon,amd/amdgpu}/ps4_bridge.c
 * PCI_DEVICE_ID_CUH_11XX 0x9920, CUH_12XX 0x9922. Southbridge pairings: sdhci-pci-core.c note (Aeolia
 * CUH-1003, Belize CUH-1215/1216). CUH-10xx is assumed to share 0x9920 (the source names 11XX only).
 * Display registers (DCE 8) were read on a 0x9920 console by src/diag run 2. */
#include "../detection/platform.h"

const struct board_profile board_fat_11 = {
    .name = "PS4 CUH-10xx/11xx (GPU 0x9920)", .board = BRD_FAT_11, .state = VS_PARTIAL, .fb_level = LVL_HWPROVEN,
    .gpu_device = 0x9920, .sb_documented = { SBF_AEOLIA, 0, 0, 0 },
};
const struct board_profile board_fat_12 = {
    .name = "PS4 CUH-12xx (GPU 0x9922)", .board = BRD_FAT_12, .state = VS_UNTESTED, .fb_level = LVL_DOCUMENTED,
    .gpu_device = 0x9922, .sb_documented = { SBF_BELIZE, 0, 0, 0 },
};
