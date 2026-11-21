# Real-time clock: no CMOS, fixed in the firmware

The PS4 has no PC-AT CMOS RTC at I/O 0x70/0x71. Sony's ACPI tables still say it has one: the FADT is
revision 4 with `IAPC_BOOT_ARCH = 0` (`reference/analysis/run2/acpi/FACP_e016c.dsl`), and to an OS that means
"legacy CMOS RTC present". Each OS port has worked around this on its own (PS4 Linux stubs
`x86_platform.get_wallclock` in `arch/x86/platform/ps4/ps4.c` and ships its own `sb-rtc.c`), and every new OS
needs yet another workaround. This firmware fixes it once, using the standard interfaces.

## What the firmware presents

| Interface | Provided by | What the OS sees |
|---|---|---|
| FADT revision 6, `IAPC_BOOT_ARCH` bit 5 "CMOS RTC not present", century/alarm CMOS indexes 0, `FIX_RTC` set, `RTC_S4` clear | `Ps4AcpiDxe` (`NoCmosRtcFadt`), only when `Ps4RtcDxe` found the counter | no CMOS RTC, so do not touch 0x70/0x71 |
| ACPI Time and Alarm Device `\_SB.PTAD` (`ACPI000E`): `_GCP` = 4, `_GRT`, `_SRT` | `Ps4Dsdt.asl`, wired up by `Ps4AcpiDxe` | the wall clock |
| EFI runtime `GetTime` / `SetTime` | `Ps4RtcDxe` (DXE runtime driver) | the same wall clock |

Both clocks share one state, the **RTC mailbox** (`Ps4Pkg/Include/Guid/Ps4RtcMailbox.h`, a runtime-data page
below 4 GiB, also published as the configuration table `gPs4RtcMailboxGuid`):

    unix time = 2012-01-01 00:00:00 UTC + counter + Offset

- **counter**: the battery-backed seconds counter of the Aeolia/Belize glue function 00:14.4, BAR2 + 0x130.
  It is four 32-bit registers holding one byte each, low byte first. A read counts once two reads 100 µs
  apart agree, and fails when any register reads all ones (00:14.4 no longer decoding: memory decode off, D3,
  or BAR2 moved), so `GetTime` returns `EFI_DEVICE_ERROR` and `_GRT` returns Valid = 0 instead of a frozen
  time. The C side is `Ps4Pkg/Include/Ps4RtcCounter.h` (`Ps4RtcDxe`, `Ps4LogDxe`); `RAWC`/`CNOW` in the
  AML follow the same rules. Sources: PS4 Linux `drivers/ps4/sb-rtc.c` + `sb-aeolia.c` (`rtc_bar 2`, `rtc_reg 0x130`,
  `rtc_byte_regs`, `rtc_epoch 1325376000`), and the 9.00 kernel's RTC subfunction (13) attach. On the Fat,
  BAR2 = 0xD0100000 (hwdump), and the diag payload already read BAR2 + 0x1100 on hardware (run 2).
  The page is described to the OS as `EFI_MEMORY_RUNTIME` MMIO.
- **Offset**: the wall-clock setting. `SetTime` and `_SRT` both rewrite it, so a change made through one
  interface shows up in the other. Both validate the same way, TimeZone included (2047 or -1440..1440) and
  Daylight (bits 0-1 only), so neither can store a value the other's reader would then hand out.

## Why each piece is needed (evidence)

- **Windows 10 22H2** (ntoskrnl + its public PDB):
  - `HalpSetPlatformFlags` checks `IAPC_BOOT_ARCH` **only if FADT revision ≥ 5**. When bit 5 is set it clears
    platform flag bit 2.
  - `HalQueryRealTimeClock` then calls `HalpQueryAcpiRealTimeClock` (ACPI TAD) instead of `HalpReadCmosTime`.
  - `HalpRtcDiscover` uses the same check (revision ≥ 5 and bit 5) to skip registering the CMOS RTC as a HAL
    timer source. That matters for the open HAL timer problem (`docs/WINDOWS10.md`).

  On Sony's revision-4 FADT the bit would be ignored, so the FADT is rebuilt as revision 6. Sony's 244 bytes are
  copied, and the new fields (sleep control/status registers, hypervisor id) stay zero.
- **Linux** (7.3 tree):
  - `acpi_parse_fadt`: `ACPI_FADT_NO_CMOS_RTC` sets `x86_platform.legacy.rtc = 0`, so no `rtc_cmos` platform
    device is registered.
  - `efisubsys_init` registers `rtc-efi` when the EFI time services work.
  - `acpi_tad` registers an RTC class device for `ACPI000E`.
  - `mach_get_cmos_time` still probes 0x70/0x71 once with a 1 s bound, then falls back to the RTC class device.
  - The PS4-specific `dummy_get_wallclock` stub and `sb-rtc.c` are no longer required for a correct clock.
- **macOS**: AppleRTC programs CMOS ports directly and is not steered by the FADT, so the firmware cannot
  remove that dependency (there is no SVM on Jaguar to trap I/O). It stays an item for the macOS track.

## Limits (current state)

- **Offset is not persistent yet.** Sony keeps the user's offset in syscon NVS, read over ICC (PS4 Linux:
  ICC command 3/1, keys at +0x6c and +0x1a8 of the reply), and this firmware has no ICC driver yet. The reset
  library needs ICC as well. Until that exists, each boot seeds Offset from OrbisOS wall time captured by the
  directboot stage, or else from `PcdPs4RtcFallbackUnixTime`, and the mailbox flags record which one was used.
  On hardware, OrbisOS `clock_gettime(CLOCK_REALTIME)` returned 9867 (looks like uptime), so today the
  build-time fallback is what gets used. An OS that syncs time (NTP / Windows Time) corrects it for the session.
  Next step: an ICC client (shared with reset/power-off) that reads the NVS offset, like Linux does.
- No wake alarms (`_GCP` bit 0/1 clear, `GetWakeupTime` / `SetWakeupTime` unsupported).
- The counter address is the BAR2 value at boot. An OS that moves 00:14.4 BAR2 would break both EFI time and
  the TAD. Neither Linux nor Windows moves a BAR that firmware assigned and that has no driver.
- No counter, no change: when `Ps4RtcDxe` finds no usable counter (00:14.4 not Aeolia/Belize, decode off, the
  counter does not answer, or the page cannot be mapped for runtime), `\_SB.PTAD._STA` returns 0 and the FADT
  is left as it was, so the OS is never left with neither a CMOS RTC nor a TAD. On a PS4 that means the old
  behaviour (EFI time from the TSC fallback, CMOS still advertised).
- QEMU harness: there is no Aeolia, so `Ps4RtcDxe` uses the TSC fallback, `\_SB.PTAD._STA` returns 0, and with
  `DBFLAGS=1` the FADT keeps its (real, emulated) CMOS RTC. The AML is tested on its own in `acpiexec`.

## Tests

- `python3 -I tools/rtc_amltest.py`: compiles the DSDT plus a generated SSDT and runs `\_SB.PTAD` in
  ACPICA `acpiexec`. It covers 60 `_GRT` cases (leap days, 2100, counter wrap at 0xFFFFFFFF, negative
  offsets), 6 `_SRT` round trips (checking the stored Offset), 4 invalid dates and 6 invalid TimeZone/Daylight
  values rejected (Offset and TimeZone left unchanged), 4 edge TimeZones accepted, each of the 4 counter
  registers reading all ones (`_GRT` Valid = 0, `_SRT` fails), and `_STA` both ways. `make hosttest` fails
  when it does.
  Every result is checked against Python's calendar.
- `make qemu`: matrix. The `ps4-dsdt` config logs `Ps4Acpi: no Aeolia RTC counter (no ACPI TAD); FADT keeps
  the CMOS RTC`. The revision-6 rebuild (`FADT rev ... -> rev 6 ... IAPC_BOOT_ARCH 20`) only runs on hardware.
- On hardware: `ps4log` prints the line
  `rtc: Aeolia counter + ACPI TAD, counter d0100130 (N at seed, M now), seed … from …`.
  Two reports a minute apart should show M advancing by about 60.
