/** @file
  TimerLib on the TSC for the PS4 Fat.

  Why not upstream: BaseCpuTimerLib needs CPUID leaf 0x15 (absent on Jaguar, family 16h), and
  the FADT's ACPI PM timer (I/O 0x1008) was never registered by Linux on this console, so it is
  not trusted. The TSC is invariant and is what Linux ran on (clocksource=tsc, 1593.719 MHz).
**/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>

STATIC
VOID
TscWait (
  IN UINT64  Ticks
  )
{
  UINT64  Start;

  Start = AsmReadTsc ();
  while (AsmReadTsc () - Start < Ticks) {
    CpuPause ();
  }
}

UINTN
EFIAPI
MicroSecondDelay (
  IN UINTN  MicroSeconds
  )
{
  TscWait (DivU64x32 (MultU64x64 (MicroSeconds, PcdGet64 (PcdPs4TscHz)), 1000000));
  return MicroSeconds;
}

UINTN
EFIAPI
NanoSecondDelay (
  IN UINTN  NanoSeconds
  )
{
  TscWait (DivU64x32 (MultU64x64 (NanoSeconds, PcdGet64 (PcdPs4TscHz)), 1000000000));
  return NanoSeconds;
}

UINT64
EFIAPI
GetPerformanceCounter (
  VOID
  )
{
  return AsmReadTsc ();
}

UINT64
EFIAPI
GetPerformanceCounterProperties (
  OUT UINT64  *StartValue  OPTIONAL,
  OUT UINT64  *EndValue    OPTIONAL
  )
{
  if (StartValue != NULL) {
    *StartValue = 0;
  }

  if (EndValue != NULL) {
    *EndValue = MAX_UINT64;
  }

  return PcdGet64 (PcdPs4TscHz);
}

UINT64
EFIAPI
GetTimeInNanoSecond (
  IN UINT64  Ticks
  )
{
  UINT64  Hz;
  UINT64  Seconds;
  UINT64  Remainder;

  Hz      = PcdGet64 (PcdPs4TscHz);
  Seconds = DivU64x64Remainder (Ticks, Hz, &Remainder);
  return MultU64x32 (Seconds, 1000000000) + DivU64x64Remainder (MultU64x32 (Remainder, 1000000000), Hz, NULL);
}
