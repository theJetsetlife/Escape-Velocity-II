# Third-party code in this tree

| Path | Origin | License |
|---|---|---|
| `Ps4Pkg/Ps4NtfsDxe/` | OpenCorePkg 1.0.8 `Platform/OpenNtfsDxe` (see `UPSTREAM.txt`; one helper replaced) | BSD-3-Clause |
| `Ps4Pkg/Ps4YukonDxe/` | linaro-home/OpenPlatformPkg @ 06d638df `Drivers/Net/MarvellYukonDxe` (ARM Ltd + FreeBSD msk) | BSD |
| `Ps4Pkg/Library/Ps4FbSerialPortLib/Font8x19.h` | generated from edk2 `GraphicsConsoleDxe/LaffStd.c` | BSD-2-Clause-Patent |
| edk2 (fetched by `scripts/setup-edk2.sh`, not in the tree) | tianocore/edk2 @ 2970e569 | BSD-2-Clause-Patent (+ submodule licenses) |
| `reference/` | various, see `reference/NOTICE.md` | their own; some proprietary |

Constants and register sequences ported from PS4 Linux drivers (Aeolia, hid-playstation, snd_hdac, the PS4 Linux
loader's VRAM setup) are cited in the comments where used.
