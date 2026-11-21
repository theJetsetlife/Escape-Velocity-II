# Windows 10 on PS4 - progress (rebuild2, 2026-11-20)

Goal: Windows 10 booted from external USB on the PS4 Fat (9.00). Media: Win10 22H2 x64 ISO; setup stick
image win10/winstick.img (GPT + FAT32, install.wim left out). Harness: qemu/run.sh WIN_IMG=... (+ DBFLAGS).

## Done (QEMU, h64 PS4-sim + our UPL)
1. ACPI_BIOS_ERROR fixed: FACS outside the memory map -> Ps4AcpiDxe copies it into ACPI NVS (also fixes
   Sony's unaligned FACS at 0xE012C).
2. ACPI_BIOS_ERROR 0x2 (root resources) fixed: memmap.c leaves 0xA0000-0xBFFFF a hole (stage abd4c03c).
   -> Windows 10 Setup first screen reached.
3. Own DSDT (Ps4Pkg/Ps4AcpiDxe/Ps4Dsdt.asl -> Ps4Dsdt.h via tools/mkdsdt.sh): PNP0A08 root bridge with bus
   range, I/O, 32-bit windows around ECAM and a 64-bit window, all measured at boot; PNP0C02 reserves
   ECAM; no _PRT/RTC/HPET/LPC; installed on Sony tables (OEM "SIE") or DBFLAGS bit0 in QEMU.
   -> Windows 10 Setup reached with this DSDT (DBFLAGS=1).
4. No-IOAPIC question answered: Sony-shaped MADT (DBFLAGS bit1: LAPICs only, PCAT 0) ->
   HAL_INITIALIZATION_FAILED 0x5C (0x200, ..., C000000E no such device). Windows needs an IOAPIC entry.
5. RAM IOAPIC (Ps4AcpiDxe, when the MADT has none; ID 8, IOWIN preloaded 0x00170011) ->
   still 0x5C but (0x110, ptr, 0, C000000D invalid parameter); the record belongs to HAL extension
   VEN_MSFT&DEV_4100&SUBDEV_0007. Experiments running: PCAT 1 + RAM IOAPIC (DBFLAGS=7) and
   real IOAPIC + PCAT 0 + no ISO/NMI (DBFLAGS=11).

## Bugcheck reading method (no debugger)
QMP GUEST_PANICKED (-cpu host,hv-crash) -> pmemsave 0..2G -> find ntoskrnl.exe PE in the dump, read the
exported KiBugCheckData (code + 4 params). Virtual params: walk page tables with CR3 from run.txt.
Scripts kept in backups/<snapshot>/tools-win (qmpdump.py, kibc.py, v2p.py, wintry.sh).

## Still open for the PS4
FADT: SCI 9 with no IOAPIC route, PM timer at 0x1008 does not count, not HW-reduced; HAL timer source
(no PIT/HPET/RTC on PS4); Windows USB (Aeolia xHCI x3 behind one function -> mf.sys split, Phase 5).
Nothing of rebuild2 has run on the PS4 yet; rebuild/ (d7a705d2 + ff86e20d) is what is served.
