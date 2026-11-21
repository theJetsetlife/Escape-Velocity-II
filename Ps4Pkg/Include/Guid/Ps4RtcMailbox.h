/** @file
  PS4 RTC mailbox: the one wall-clock state shared by the EFI runtime time services (Ps4RtcDxe)
  and the ACPI Time and Alarm Device (ACPI000E, \_SB.PTAD in Ps4Dsdt.asl).

  The PS4 has no PC-AT CMOS RTC at I/O 0x70/0x71. Its battery-backed clock is a free-running
  seconds counter in the Aeolia/Belize glue function (00:14.4 BAR2 + 0x130, four 32-bit registers,
  one byte each, little-endian), counting from 2012-01-01 00:00:00 UTC. Sony keeps the user's
  wall-clock offset outside the counter (syscon NVS, read over ICC), so:

      unix time = PS4_RTC_EPOCH + counter + Offset

  Producer: Ps4RtcDxe (EfiRuntimeServicesData page below 4 GiB, published as a configuration
  table under this GUID). Consumers: Ps4AcpiDxe (patches the physical addresses into the DSDT),
  the AML of \_SB.PTAD (reads the counter and Offset, _SRT rewrites Offset), Ps4LogDxe (report).
  The layout is fixed: the AML Field list in Ps4Dsdt.asl mirrors it byte for byte.
**/

#ifndef PS4_RTC_MAILBOX_H_
#define PS4_RTC_MAILBOX_H_

#define PS4_RTC_MAILBOX_GUID \
  { 0xa1e36d2e, 0x2363, 0x4beb, { 0x92, 0xac, 0x52, 0xcc, 0x61, 0xd1, 0x28, 0xc2 } }

#define PS4_RTC_MAILBOX_SIGNATURE  SIGNATURE_32 ('P', '4', 'R', 'T')
#define PS4_RTC_MAILBOX_VERSION    1

/// 2012-01-01 00:00:00 UTC: counter zero (PS4 Linux sb-aeolia.c rtc_epoch, 9.00 kernel RTC attach)
#define PS4_RTC_EPOCH              1325376000LL
/// counter registers inside 00:14.4 BAR2
#define PS4_RTC_COUNTER_OFFSET     0x130

/// Flags
#define PS4_RTC_FLAG_COUNTER       BIT0   ///< Aeolia counter found and mapped for runtime; ACPI TAD present
#define PS4_RTC_FLAG_SEED_ORBIS    BIT1   ///< Offset came from OrbisOS wall time at takeover
#define PS4_RTC_FLAG_SEED_BUILD    BIT2   ///< Offset came from PcdPs4RtcFallbackUnixTime (time is a guess)
#define PS4_RTC_FLAG_SET           BIT3   ///< SetTime/_SRT changed Offset in this boot

#pragma pack(1)
typedef struct {
  UINT32    Signature;     ///< +0x00 PS4_RTC_MAILBOX_SIGNATURE
  UINT32    Version;       ///< +0x04
  UINT32    Flags;         ///< +0x08 PS4_RTC_FLAG_*
  UINT32    Reserved0;     ///< +0x0C
  INT64     Offset;        ///< +0x10 unix = PS4_RTC_EPOCH + counter + Offset (two's complement)
  INT16     TimeZone;      ///< +0x18 EFI_TIME.TimeZone / _SRT TimeZone (minutes, 2047 = unspecified)
  UINT8     Daylight;      ///< +0x1A
  UINT8     Reserved1;     ///< +0x1B
  UINT32    Reserved2;     ///< +0x1C
  UINT64    CounterBase;   ///< +0x20 physical address of the counter registers (0 = none)
  UINT64    SeedUnix;      ///< +0x28 the seed used at boot (diagnostics)
  UINT32    SeedCounter;   ///< +0x30 counter value at seeding (diagnostics)
  UINT32    Reserved3;     ///< +0x34
} PS4_RTC_MAILBOX;
#pragma pack()

extern EFI_GUID  gPs4RtcMailboxGuid;

#endif
