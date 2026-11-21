# Escape Velocity II — native UEFI firmware for the PS4 Fat (9.00)

Boots a real UEFI environment (EDK II, UEFI Shell) on a PlayStation 4 "Fat" (Aeolia southbridge) on system
software 9.00, with no Linux in between: a ring-0 **directboot stage** takes the machine over from OrbisOS and
hands it to an **EDK II Universal Payload** built from unmodified upstream edk2 plus `Ps4Pkg/`.

Goal of the current work: 64-bit OS loaders, all 6 GiB of system RAM, NTFS, one-command logging, and
Windows 10 booting from an external USB drive.

**Start with [HANDOFF.md](HANDOFF.md)** — current state, what is proven on hardware vs only in QEMU, the
rules learned the hard way, and the next steps.

## Status at a glance
| Area | State |
|---|---|
| Delivery (WebKitty → Lapse → GoldHEN slot) → ring 0 → takeover → EDK II → UEFI Shell on screen | **proven on hardware** (stage `ff86e20d` + UEFI `5c32a84d`, `prebuilt/hw-verified/`) |
| USB (xHCI ×3), SATA, SD, UART, DualShock 4 as keyboard, Marvell Yukon Ethernet | built + QEMU-tested (`prebuilt/io-build/`, UEFI `d7a705d2`); **not yet run on hardware** |
| 6 GiB RAM (2 GiB VRAM carve), DMA bounce < 2 GiB, NTFS (read-only), `ps4log` | built + QEMU-tested in this tree; **not yet run on hardware** |
| Other system software (5.05–14.02) | stage recognises 22 versions from a per-version offset table; only 9.00 is verified and armed, the rest are refused until their `phys_avail` is known — [docs/FIRMWARE_VERSIONS.md](docs/FIRMWARE_VERSIONS.md) |
| Windows 10 | Setup reaches its first screen in QEMU through this firmware with the PS4 DSDT; needs a working IOAPIC (open problem) — [docs/WINDOWS10.md](docs/WINDOWS10.md) |

## Layout
| Path | What |
|---|---|
| `src/directboot/` | the stage: GoldHEN-slot payload, ring-0 arm, ICC hook, OrbisOS takeover, memory map, UPL load |
| `src/upl_load.c`, `src/upl_hob.h` | ELF Universal Payload loader + HOB builder (shared with the QEMU harness and host tests) |
| `src/diag/`, `src/k0probe/` | read-only diagnostic payloads used to measure the machine before the takeover existed |
| `Ps4Pkg/` | all PS4 EDK II code: drivers, libraries, `mkplatform.py` (derives the platform DSC/FDF from upstream) |
| `qemu/` | QEMU harness that imitates the PS4 handoff (stub + `h64`) — the regression gate |
| `tools/` | host tests, PC-side receiver, WebKitty server helper, deploy, DSDT generator, Windows tools |
| `server/` | docker compose for the PC side of a hardware run |
| `scripts/setup-edk2.sh` | fetches the pinned upstream edk2 (commit `2970e569`) |
| `prebuilt/` | known binaries with their status ([prebuilt/README.md](prebuilt/README.md)) |
| `docs/` | architecture, build, testing, hardware runs, Windows 10; `docs/notes/` = original engineering notes |
| `reference/` | third-party and proprietary reference material (see [reference/NOTICE.md](reference/NOTICE.md)) |

## Quick start
```sh
make setup                  # pinned upstream edk2 + BaseTools (once; ~1 GB)
make upl                    # out/upl-ps4.elf
cp server/.env.example server/.env && $EDITOR server/.env    # PC_IP, WEBKITTY_DIR
make stage                  # src/directboot/directboot.bin (PC address built in)
make hosttest               # host unit tests
make qemu                   # QEMU regression matrix -> MATRIX: PASS
```
Then [docs/HARDWARE.md](docs/HARDWARE.md) for a PS4 run. Details: [docs/BUILD.md](docs/BUILD.md),
[docs/TESTING.md](docs/TESTING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Licensing
Third-party code keeps its own license (see [THIRD_PARTY.md](THIRD_PARTY.md)). No license has been chosen yet
for the project's own code; until the owner adds a LICENSE file, treat it as all rights reserved.
