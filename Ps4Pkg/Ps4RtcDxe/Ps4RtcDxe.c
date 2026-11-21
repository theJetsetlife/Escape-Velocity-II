/** @file
  Ps4RtcDxe - Real Time Clock architectural protocol for the PS4.

  The PS4 has no PC-AT CMOS RTC at I/O 0x70/0x71, so upstream PcatRealTimeClockRuntimeDxe cannot be used.
  Its battery-backed clock is a seconds counter in the Aeolia/Belize glue function 00:14.4, BAR2 + 0x130
  (see Guid/Ps4RtcMailbox.h). This driver makes that counter THE platform clock for every OS through the
  standard interfaces, instead of each OS carrying its own PS4 RTC code:

    - EFI runtime GetTime/SetTime read the counter (EFI_MEMORY_RUNTIME MMIO mapping),
    - the same state (counter address + wall-clock Offset) is published in a runtime-data mailbox that
      Ps4AcpiDxe wires into the DSDT's ACPI Time and Alarm Device (ACPI000E), and Ps4AcpiDxe tells the OS
      "CMOS RTC not present" in a revision-6 FADT.

  Offset (the user's wall-clock setting) lives in Sony's syscon NVS behind ICC, which this firmware does not
  drive yet. Until then Offset is seeded every boot from OrbisOS's wall time captured by the directboot stage
  (gPs4DirectbootInfoGuid), or from PcdPs4RtcFallbackUnixTime; SetTime/_SRT change it for the current boot.

  Fallback (no Aeolia/Belize glue function, e.g. the QEMU harness, or the counter does not answer): the
  previous behaviour, seed + TSC time elapsed since this driver started; the ACPI TAD then reports absent.
**/

#include <PiDxe.h>
#include <Guid/AcpiBoardInfoGuid.h>
#include <Guid/EventGroup.h>
#include <Guid/Ps4DirectbootInfo.h>
#include <Guid/Ps4RtcMailbox.h>
#include <Ps4RtcCounter.h>
#include <Protocol/RealTimeClock.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define SONY_VENDOR            0x104D
#define GLUE_DEVICE            0x14
#define GLUE_FUNCTION          4
#define AEOLIA_GLUE_ID         0x90A1           // pci_ids.h PCI_DEVICE_ID_SONY_AEOLIA_PCIE
#define BELIZE_GLUE_ID         0x90CC           // PCI_DEVICE_ID_SONY_BELIZE_PCIE (same RTC layout)

/* the AML Field list of \_SB.PTAD (Ps4Dsdt.asl) relies on these offsets */
STATIC_ASSERT (OFFSET_OF (PS4_RTC_MAILBOX, Signature) == 0x00, "mailbox layout: MSIG");
STATIC_ASSERT (OFFSET_OF (PS4_RTC_MAILBOX, Flags) == 0x08, "mailbox layout: MFLG");
STATIC_ASSERT (OFFSET_OF (PS4_RTC_MAILBOX, Offset) == 0x10, "mailbox layout: MOFS");
STATIC_ASSERT (OFFSET_OF (PS4_RTC_MAILBOX, TimeZone) == 0x18, "mailbox layout: MTZ");
STATIC_ASSERT (OFFSET_OF (PS4_RTC_MAILBOX, Daylight) == 0x1A, "mailbox layout: MDL");
STATIC_ASSERT (PS4_RTC_MAILBOX_SIGNATURE == 0x54523450, "AML _STA compares against 0x54523450");

STATIC PS4_RTC_MAILBOX  *mMailbox;             // runtime data; converted at SetVirtualAddressMap
STATIC UINTN            mCounter;               // counter MMIO (0 = TSC fallback); converted as well
STATIC UINT64           mTscAtStart;            // fallback clock origin
STATIC EFI_EVENT        mVirtualEvent;

/* days since 1970-01-01 -> civil date (proleptic Gregorian), H. Hinnant's algorithm */
STATIC
VOID
CivilFromDays (
  IN  INT64   Days,
  OUT UINT16  *Year,
  OUT UINT8   *Month,
  OUT UINT8   *Day
  )
{
  INT64  Era, Doe, Yoe, Doy, Mp, Y;

  Days += 719468;
  Era   = (Days >= 0 ? Days : Days - 146096) / 146097;
  Doe   = Days - Era * 146097;
  Yoe   = (Doe - Doe / 1460 + Doe / 36524 - Doe / 146096) / 365;
  Y     = Yoe + Era * 400;
  Doy   = Doe - (365 * Yoe + Yoe / 4 - Yoe / 100);
  Mp    = (5 * Doy + 2) / 153;
  *Day   = (UINT8)(Doy - (153 * Mp + 2) / 5 + 1);
  *Month = (UINT8)(Mp < 10 ? Mp + 3 : Mp - 9);
  *Year  = (UINT16)(Y + (*Month <= 2));
}

STATIC
INT64
DaysFromCivil (
  IN UINT16  Year,
  IN UINT8   Month,
  IN UINT8   Day
  )
{
  INT64  Y, Era, Yoe, Doy, Doe;

  Y   = (INT64)Year - (Month <= 2);
  Era = (Y >= 0 ? Y : Y - 399) / 400;
  Yoe = Y - Era * 400;
  Doy = (153 * (Month > 2 ? Month - 3 : Month + 9) + 2) / 5 + Day - 1;
  Doe = Yoe * 365 + Yoe / 4 - Yoe / 100 + Doy;
  return Era * 146097 + Doe - 719468;
}

/* Seconds of the platform clock without Offset: PS4_RTC_EPOCH + counter, or TSC seconds since start. */
STATIC
BOOLEAN
ClockNow (
  OUT INT64   *Seconds,
  OUT UINT32  *Nanosecond
  )
{
  UINT32  Ticks;
  UINT64  Ns;

  if (mCounter != 0) {
    if (!Ps4RtcCounterRead (mCounter, &Ticks)) {
      return FALSE;
    }

    *Seconds    = PS4_RTC_EPOCH + (INT64)Ticks;
    *Nanosecond = 0;
    return TRUE;
  }

  Ns          = GetTimeInNanoSecond (GetPerformanceCounter () - mTscAtStart);
  *Seconds    = (INT64)(Ns / 1000000000ULL);
  *Nanosecond = (UINT32)(Ns % 1000000000ULL);
  return TRUE;
}

/* No DEBUG() in the services below: they run as runtime services after ExitBootServices. */
STATIC
EFI_STATUS
EFIAPI
Ps4GetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities OPTIONAL
  )
{
  INT64   Unix, Days, Secs;
  UINT32  Ns;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!ClockNow (&Unix, &Ns)) {
    return EFI_DEVICE_ERROR;
  }

  Unix += mMailbox->Offset;
  Days  = Unix / 86400;
  Secs  = Unix % 86400;
  if (Secs < 0) {
    Secs += 86400;
    Days--;
  }

  CivilFromDays (Days, &Time->Year, &Time->Month, &Time->Day);
  Time->Hour       = (UINT8)(Secs / 3600);
  Time->Minute     = (UINT8)((Secs / 60) % 60);
  Time->Second     = (UINT8)(Secs % 60);
  Time->Nanosecond = Ns;
  Time->TimeZone   = mMailbox->TimeZone;
  Time->Daylight   = mMailbox->Daylight;
  Time->Pad1       = 0;
  Time->Pad2       = 0;
  if (Capabilities != NULL) {
    Capabilities->Resolution = 1;
    Capabilities->Accuracy   = 50000000;   // 50 ppm (UEFI unit: 1E-6 ppm)
    Capabilities->SetsToZero = FALSE;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ps4SetTime (
  IN EFI_TIME  *Time
  )
{
  UINT32  Ns;
  INT64   Now;
  INT64   Target;

  if ((Time == NULL) || (Time->Year < 1970) || (Time->Year > 9999) || (Time->Month < 1) ||
      (Time->Month > 12) || (Time->Day < 1) || (Time->Day > 31) || (Time->Hour > 23) ||
      (Time->Minute > 59) || (Time->Second > 59) || (Time->Nanosecond > 999999999) ||
      ((Time->TimeZone != EFI_UNSPECIFIED_TIMEZONE) && ((Time->TimeZone < -1440) || (Time->TimeZone > 1440))) ||
      ((Time->Daylight & ~(EFI_TIME_ADJUST_DAYLIGHT | EFI_TIME_IN_DAYLIGHT)) != 0))   // _SRT checks the same
  {
    return EFI_INVALID_PARAMETER;
  }

  if (!ClockNow (&Now, &Ns)) {
    return EFI_DEVICE_ERROR;
  }

  Target = DaysFromCivil (Time->Year, Time->Month, Time->Day) * 86400 +
           Time->Hour * 3600 + Time->Minute * 60 + Time->Second;
  mMailbox->Offset   = Target - Now;
  mMailbox->TimeZone = Time->TimeZone;
  mMailbox->Daylight = Time->Daylight;
  mMailbox->Flags   |= PS4_RTC_FLAG_SET;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ps4GetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
Ps4SetWakeupTime (
  IN BOOLEAN   Enable,
  IN EFI_TIME  *Time OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
VOID
EFIAPI
Ps4RtcVirtualAddressChange (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mCounter != 0) {
    gRT->ConvertPointer (0, (VOID **)&mCounter);
  }

  gRT->ConvertPointer (0, (VOID **)&mMailbox);
}

/* 00:14.4 BAR2 when it is an Aeolia/Belize glue function; 0 otherwise. Config space is only read. */
STATIC
UINT64
FindGlueBar2 (
  VOID
  )
{
  VOID                   *Hob;
  CONST ACPI_BOARD_INFO  *Board;
  UINTN                  Cfg;
  UINT32                 Id;
  UINT32                 Lo;
  UINT64                 Bar;

  Hob = GetFirstGuidHob (&gUefiAcpiBoardInfoGuid);
  if (Hob == NULL) {
    return 0;
  }

  Board = GET_GUID_HOB_DATA (Hob);
  if ((Board->PcieBaseAddress == 0) || (Board->PcieBaseSize < SIZE_1MB)) {
    return 0;
  }

  Cfg = (UINTN)Board->PcieBaseAddress + (GLUE_DEVICE << 15) + (GLUE_FUNCTION << 12);
  Id  = MmioRead32 (Cfg);
  if (((Id & 0xFFFF) != SONY_VENDOR) || (((Id >> 16) != AEOLIA_GLUE_ID) && ((Id >> 16) != BELIZE_GLUE_ID))) {
    DEBUG ((DEBUG_INFO, "Ps4RtcDxe: 00:14.4 is %08x, not an Aeolia/Belize glue function\n", Id));
    return 0;
  }

  if ((MmioRead16 (Cfg + 0x04) & BIT1) == 0) {
    DEBUG ((DEBUG_WARN, "Ps4RtcDxe: glue memory decode is off\n"));
    return 0;
  }

  Lo = MmioRead32 (Cfg + 0x18);                                  // BAR2
  if ((Lo & 1) != 0) {
    return 0;
  }

  Bar = Lo & ~0xFULL;
  if (((Lo >> 1) & 3) == 2) {
    Bar |= LShiftU64 (MmioRead32 (Cfg + 0x1C), 32);
  }

  return Bar;
}

/* Describe the counter page as runtime MMIO so the OS maps it for runtime services. */
STATIC
EFI_STATUS
MapCounterForRuntime (
  IN UINT64  Page
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Desc;
  EFI_STATUS                       Status;

  Status = gDS->GetMemorySpaceDescriptor (Page, &Desc);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Desc.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = gDS->AddMemorySpace (EfiGcdMemoryTypeMemoryMappedIo, Page, EFI_PAGE_SIZE, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else if (Desc.GcdMemoryType != EfiGcdMemoryTypeMemoryMappedIo) {
    return EFI_ACCESS_DENIED;
  } else {
    Status = gDS->SetMemorySpaceCapabilities (Page, EFI_PAGE_SIZE, Desc.Capabilities | EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return gDS->SetMemorySpaceAttributes (Page, EFI_PAGE_SIZE, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
}

EFI_STATUS
EFIAPI
Ps4RtcEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  VOID                       *Hob;
  CONST PS4_DIRECTBOOT_INFO  *Info;
  EFI_PHYSICAL_ADDRESS       Page;
  EFI_HANDLE                 Handle;
  EFI_STATUS                 Status;
  UINT64                     Bar2;
  UINT32                     Ticks;
  UINT32                     Ns;
  INT64                      Seed;
  INT64                      Now;

  mTscAtStart = GetPerformanceCounter ();
  Ticks       = 0;

  Page   = BASE_4GB - 1;                                       // below 4 GiB: plain 32-bit address for the AML
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiRuntimeServicesData, 1, &Page);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mMailbox = (PS4_RTC_MAILBOX *)(UINTN)Page;
  ZeroMem (mMailbox, EFI_PAGE_SIZE);
  mMailbox->Signature = PS4_RTC_MAILBOX_SIGNATURE;
  mMailbox->Version   = PS4_RTC_MAILBOX_VERSION;
  mMailbox->TimeZone  = EFI_UNSPECIFIED_TIMEZONE;

  /* hardware counter */
  Bar2 = FindGlueBar2 ();
  if (Bar2 != 0) {
    if (!Ps4RtcCounterRead ((UINTN)(Bar2 + PS4_RTC_COUNTER_OFFSET), &Ticks)) {
      DEBUG ((DEBUG_WARN, "Ps4RtcDxe: RTC counter at %lx does not answer; TSC fallback\n", Bar2 + PS4_RTC_COUNTER_OFFSET));
    } else if (EFI_ERROR (Status = MapCounterForRuntime (Bar2 & ~(UINT64)EFI_PAGE_MASK))) {
      DEBUG ((DEBUG_WARN, "Ps4RtcDxe: cannot map RTC page %lx for runtime (%r); TSC fallback\n", Bar2, Status));
    } else {
      mCounter              = (UINTN)(Bar2 + PS4_RTC_COUNTER_OFFSET);
      mMailbox->CounterBase = Bar2 + PS4_RTC_COUNTER_OFFSET;
      mMailbox->SeedCounter = Ticks;
      mMailbox->Flags      |= PS4_RTC_FLAG_COUNTER;
    }
  }

  /* wall-clock seed */
  Seed             = (INT64)PcdGet64 (PcdPs4RtcFallbackUnixTime);
  mMailbox->Flags |= PS4_RTC_FLAG_SEED_BUILD;
  Hob              = GetFirstGuidHob (&gPs4DirectbootInfoGuid);
  if (Hob != NULL) {
    Info = GET_GUID_HOB_DATA (Hob);
    // HW attempt 19:13 UTC: OrbisOS CLOCK_REALTIME read 9867 (looks like uptime) - only
    // accept a plausible wall-clock value (after 2017-07-14).
    if ((Info->Version == PS4_DIRECTBOOT_INFO_VERSION) && (Info->UnixTime > 1500000000ULL)) {
      Seed            = (INT64)Info->UnixTime;
      mMailbox->Flags = (mMailbox->Flags & ~PS4_RTC_FLAG_SEED_BUILD) | PS4_RTC_FLAG_SEED_ORBIS;
    }
  }

  if (!ClockNow (&Now, &Ns)) {
    Now = PS4_RTC_EPOCH + (INT64)Ticks;
  }

  mMailbox->SeedUnix = (UINT64)Seed;
  mMailbox->Offset   = Seed - Now;

  DEBUG ((DEBUG_INFO, "Ps4RtcDxe: %a clock, counter %lx = %u, seed %ld (%a), offset %ld, mailbox %p\n",
          (mCounter != 0) ? "Aeolia RTC" : "TSC fallback", mMailbox->CounterBase, mMailbox->SeedCounter, Seed,
          ((mMailbox->Flags & PS4_RTC_FLAG_SEED_ORBIS) != 0) ? "OrbisOS" : "build fallback", mMailbox->Offset, mMailbox));

  Status = gBS->InstallConfigurationTable (&gPs4RtcMailboxGuid, mMailbox);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Ps4RtcVirtualAddressChange, NULL,
                               &gEfiEventVirtualAddressChangeGuid, &mVirtualEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gRT->GetTime       = Ps4GetTime;
  gRT->SetTime       = Ps4SetTime;
  gRT->GetWakeupTime = Ps4GetWakeupTime;
  gRT->SetWakeupTime = Ps4SetWakeupTime;

  Handle = NULL;
  return gBS->InstallMultipleProtocolInterfaces (
                &Handle,
                &gEfiRealTimeClockArchProtocolGuid,
                NULL,
                NULL
                );
}
