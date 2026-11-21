# Testing

## 1. Host tests (`make hosttest`)
| Test | Checks |
|---|---|
| `tools/upl_hosttest.c` | `src/upl_load.c` against the real `out/upl-ps4.elf`: ELF parsing, relocations, FV location, HOB list |
| `tools/memmap_hosttest.c` | `src/directboot/memmap.c` rules: VRAM carve not RAM, released high RAM, device-shared ranges, handoff reservations win, fail closed without phys_avail |
| `tools/ds4_hosttest.c` | DualShock 4 report → key translation (`Ps4Pkg/Ps4Ds4Dxe/Ds4Report.c`) |
| `tools/fwsel_hosttest.c` | stage version selection + prologue decoder; with `KDUMP=<9.00 kernel dump>` exactly the 9.00 row must match and decode to the 15 hardware-proven bytes |

## 2. QEMU regression gate (`make qemu` → `qemu/matrix.sh`)
Every configuration must print `RESULT: PASS` (UEFI Shell reached, no exception/ASSERT) before any hardware run.

| Mode (env for `qemu/run.sh`) | Imitates / checks |
|---|---|
| `SMP=8` (default), `SMP=1` | 7 parked APs must stay parked (MpInitLibUp) |
| `STALE_LAPIC=1` | handoff with vector 0x40 in service + 0x30 pending (takeover inside an interrupt handler) |
| `MEM8G=1` | PS4-like 8 GiB: VRAM carve + released high RAM (~5.3 GB available in the UEFI map) |
| `IO_TEST=1` | xHCI + USB keyboard + FAT and NTFS sticks + AHCI disk, UART mirror; files listed in the Shell |
| `NET_TEST=1` | virtio-net on QEMU user networking (DHCP/DNS/TFTP), through the PciIo shim |
| `LOG_TEST=1` (+IO+NET, `SHELL_CMDS=ps4log`) | `ps4log` output and the boot auto-save are checked on the stick image |
| `DBFLAGS=n` | `PS4_DIRECTBOOT_INFO.Flags`: 1 PS4 DSDT, 2 Sony-shaped MADT, 4 keep PCAT, 8 keep real IOAPIC |
| `WIN_IMG=img` | only a USB keyboard + that image as a USB stick (Windows tests) |
| `SHELL_CMDS='a;b'`, `SHELL_WAIT=s` | type Shell commands on the USB keyboard, wait after them |

Outputs in `qemu/out/`: `debug.log` (stub/h64 + EDK II DEBUG via debugcon), `screen.png`, `serial.log`.
Failed runs keep their evidence in `qemu/out/fail-<time>/`. One intermittent STALE_LAPIC failure (1 in 5) was
seen once and not explained; rerun and keep the evidence if it shows up.

## 3. `ps4log` (on hardware and in QEMU)
At the UEFI Shell: `ps4log [-o fsN:] [-dns <host>] [-nonet]` writes `fsN:\ps4log\YYYYMMDD-HHMMSS.txt` (FAT; NTFS
is read-only): boot log (every DEBUG line since the takeover), hardware dump (CPU/MSRs/LAPIC, UEFI memory map,
ACPI incl. DSDT/FACS location and memory type, PCI bus 0 read-only, southbridge MSI block, GOP/block/FS/USB/NIC),
network tests (link, DHCP, DNS, PXE offer, TFTP size) with PASS/FAIL lines. A boot-time copy (without network)
is written to the first writable USB volume as `\ps4log\boot-*.txt`.

## 4. Windows 10 in QEMU
`tools/win10/mkwinstick.sh Win10_22H2_English_x64.iso` → `out/winstick.img`, then
`WIN_IMG=out/winstick.img make qemu` (adds a Setup boot check), or individual experiments with
`tools/win10/wintry.sh <DBFLAGS> <label>` (paths inside the script assume this tree; adjust `X=`).

## 5. Reading a Windows bugcheck without a debugger
Run with `QCPU=host,hv-crash` and a QMP socket (`QEMU_EXTRA="-qmp unix:…,server,nowait"`). `tools/win10/qmpdump.py`
waits for `GUEST_PANICKED` and `pmemsave`s guest RAM; `tools/win10/kibc.py <ram.bin>` finds `ntoskrnl.exe` in the
dump and prints its exported `KiBugCheckData` (code + 4 parameters); `tools/win10/v2p.py <ram.bin> <CR3> <va>`
translates a parameter address through the guest page tables (CR3 from `qemu/out/run.txt`). QEMU's own
hv-crash event carries no parameters, hence this route.
