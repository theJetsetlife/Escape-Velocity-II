/** @file
  Ps4RtcDxe - Real Time Clock architectural protocol for the PS4 Fat.

  The PS4 has no CMOS RTC at I/O 0x70/0x71 (the real clock is an Aeolia sub-function behind
  ICC), so upstream PcatRealTimeClockRuntimeDxe cannot be used. Time = OrbisOS CLOCK_REALTIME
  captured by the directboot stage (gPs4DirectbootInfoGuid) + TSC time elapsed since this
  driver started. SetTime moves the offset (volatile: lost at power-off). No wakeup alarm.
**/

#include <PiDxe.h>
#include <Guid/Ps4DirectbootInfo.h>
#include <Protocol/RealTimeClock.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC INT64   mUnixAtStart;     // seconds since 1970-01-01 when mTscAtStart was taken
STATIC UINT64  mTscAtStart;
STATIC INT16   mTimeZone = EFI_UNSPECIFIED_TIMEZONE;
STATIC UINT8   mDaylight;

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
  INT64   Era, Doe, Yoe, Doy, Mp, Y;

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

STATIC
INT64
NowUnix (
  OUT UINT32  *Nanosecond
  )
{
  UINT64  Ns;

  Ns          = GetTimeInNanoSecond (GetPerformanceCounter () - mTscAtStart);
  *Nanosecond = (UINT32)(Ns % 1000000000ULL);
  return mUnixAtStart + (INT64)(Ns / 1000000000ULL);
}

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

  Unix = NowUnix (&Ns);
  Days = Unix / 86400;
  Secs = Unix % 86400;
  CivilFromDays (Days, &Time->Year, &Time->Month, &Time->Day);
  Time->Hour       = (UINT8)(Secs / 3600);
  Time->Minute     = (UINT8)((Secs / 60) % 60);
  Time->Second     = (UINT8)(Secs % 60);
  Time->Nanosecond = Ns;
  Time->TimeZone   = mTimeZone;
  Time->Daylight   = mDaylight;
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
  INT64   Target;

  if ((Time == NULL) || (Time->Year < 1970) || (Time->Year > 9999) || (Time->Month < 1) ||
      (Time->Month > 12) || (Time->Day < 1) || (Time->Day > 31) || (Time->Hour > 23) ||
      (Time->Minute > 59) || (Time->Second > 59) || (Time->Nanosecond > 999999999))
  {
    return EFI_INVALID_PARAMETER;
  }

  Target = DaysFromCivil (Time->Year, Time->Month, Time->Day) * 86400 +
           Time->Hour * 3600 + Time->Minute * 60 + Time->Second;
  mUnixAtStart += Target - NowUnix (&Ns);
  mTimeZone     = Time->TimeZone;
  mDaylight     = Time->Daylight;
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

EFI_STATUS
EFIAPI
Ps4RtcEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  VOID                       *Hob;
  CONST PS4_DIRECTBOOT_INFO  *Info;
  EFI_HANDLE                 Handle;

  mTscAtStart  = GetPerformanceCounter ();
  mUnixAtStart = (INT64)PcdGet64 (PcdPs4RtcFallbackUnixTime);
  Hob          = GetFirstGuidHob (&gPs4DirectbootInfoGuid);
  if (Hob != NULL) {
    Info = GET_GUID_HOB_DATA (Hob);
    // HW attempt 19:13 UTC: OrbisOS CLOCK_REALTIME read 9867 (looks like uptime) - only
    // accept a plausible wall-clock value (after 2017-07-14).
    if ((Info->Version == PS4_DIRECTBOOT_INFO_VERSION) && (Info->UnixTime > 1500000000ULL)) {
      mUnixAtStart = (INT64)Info->UnixTime;
    }
  }

  DEBUG ((DEBUG_INFO, "Ps4RtcDxe: seeded with unix time %ld\n", mUnixAtStart));
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
