# Windows 10 on the PS4 — findings and plan

Target: Windows 10 x64 (22H2) booting from an external USB drive. Windows 7 was dropped: it needs a legacy PIC,
INT 10h video and has no xHCI driver. Windows 10 works with GOP, an inbox xHCI class driver and ACPI.

## Found and fixed (QEMU, this firmware)
1. **ACPI_BIOS_ERROR — FACS outside the memory map.** edk2 AcpiTableDxe keeps a FACS from the bootloader HOB in
   place; Sony's (0xE012C, also not 64-byte aligned) and the harness's are in memory the UEFI map does not describe.
   `Ps4AcpiDxe` copies it into an ACPI NVS page and repoints the FADT.
2. **ACPI_BIOS_ERROR 0x2 (root bridge resources).** A memory-map entry over 0xA0000–0xBFFFF collided with the root
   bridge `_CRS`. `memmap.c` leaves that range out. → **Windows Setup first screen reached.**
3. **Sony's DSDT is unusable for Windows** (PCI0 `_CRS` = only I/O 0xCF8: no bus range, no windows).
   `Ps4AcpiDxe` installs `Ps4Dsdt.asl` instead: PNP0A08 root bridge, bus 0..N, I/O, 32-bit windows from the top of low
   RAM to 0xFEBFFFFF around ECAM, a 64-bit window above TOM2 (below AMD's HyperTransport hole), PNP0C02 reserving
   ECAM, processor objects named as Sony's SSDT expects, no `_PRT` (no IOAPIC; devices use MSI). Every live BAR is
   checked against the windows at boot (config reads only). → Setup reached with this DSDT (`DBFLAGS=1`), which also
   hides QEMU's RTC/HPET/LPC/keyboard controller — a fair proxy for the PS4.

## Open: the IOAPIC
| MADT given to Windows (QEMU) | Result |
|---|---|
| Sony shape: 8 LAPICs, no IOAPIC, PCAT_COMPAT 0 | HAL_INITIALIZATION_FAILED 0x5C (0x200, …, C000000E no such device) |
| + RAM page as IOAPIC (ID 8, IOWIN preloaded 0x00170011) | 0x5C (0x110, ptr, 0, C000000D invalid parameter), record of HAL extension `VEN_MSFT&DEV_4100&SUBDEV_0007` |
| same with PCAT_COMPAT 1 | same 0x5C/0x110 |
| QEMU's real IOAPIC, PCAT 0, no overrides/NMI | **Setup reached** |
Conclusion so far: Windows needs an IOAPIC whose registers answer like one; a plain RAM page does not (reads always
return the last value written to IOWIN, not the indexed register). In the tree, untested: RAM IOAPIC with MADT ID 0
(the ID field its reads return) — `DBFLAGS=3` with `tools/win10/wintry.sh 3 ramioapic-id0`.
Further options: a thin AMD-SVM hypervisor emulating an IOAPIC page (check SVM/VM_CR in `ps4log`), or a southbridge
MMIO block that can stand in.

## Next obstacles expected on hardware
- FADT: SCI 9 has no IOAPIC route; PM timer at 0x1008 does not count; not hardware-reduced (a clock source for the
  HAL without PIT/HPET/RTC is an open question). **RTC done in firmware** (`docs/RTC.md`): revision-6 FADT with
  "CMOS RTC not present" makes the HAL take wall time from the ACPI TAD (`HalpSetPlatformFlags`) and skip the CMOS
  RTC as a timer source (`HalpRtcDiscover`); both checks need FADT revision >= 5, which Sony's (4) is not.
- Windows USB: three xHCI controllers behind one PCI function (Sony class code, Aeolia MSI routing). Plan: an INF
  binding `mf.sys` to split the function, plus MSI setup; then a Windows To Go style external drive.
- Display: GOP framebuffer only (Basic Display driver).
