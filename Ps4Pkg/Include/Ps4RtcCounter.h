/** @file
  Reading the Aeolia/Belize RTC seconds counter (00:14.4 BAR2 + PS4_RTC_COUNTER_OFFSET, see
  Guid/Ps4RtcMailbox.h). Shared by Ps4RtcDxe (EFI runtime GetTime/SetTime) and Ps4LogDxe (report);
  the AML of \_SB.PTAD (RAWC/CNOW in Ps4Dsdt.asl) implements the same rules.

  The counter is four 32-bit registers holding one byte each, low byte first. A read is rejected when
  any register reads all ones (master abort: the function stopped decoding, is in D3, or BAR2 moved),
  and a value is accepted only when two reads 100 us apart agree (no carry between the byte reads).
  Callers need IoLib and TimerLib.
**/

#ifndef PS4_RTC_COUNTER_H_
#define PS4_RTC_COUNTER_H_

#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define PS4_RTC_COUNTER_RETRIES      21         // as PS4 Linux sb-rtc.c
#define PS4_RTC_COUNTER_RETRY_DELAY  100        // us

/* One unchecked read. FALSE when a register reads all ones. Runtime-safe (no DEBUG). */
static inline
BOOLEAN
Ps4RtcCounterRaw (
  IN  UINTN   Regs,
  OUT UINT32  *Value
  )
{
  UINT32  Byte;
  UINT32  Result;
  UINTN   Index;

  Result = 0;
  for (Index = 0; Index < 4; Index++) {
    Byte = MmioRead32 (Regs + 4 * Index);
    if (Byte == MAX_UINT32) {
      return FALSE;
    }

    Result |= (Byte & 0xFF) << (8 * Index);
  }

  *Value = Result;
  return TRUE;
}

/* A stable counter value: two reads 100 us apart agree. FALSE when it never settles or does not answer. */
static inline
BOOLEAN
Ps4RtcCounterRead (
  IN  UINTN   Regs,
  OUT UINT32  *Value
  )
{
  UINT32  Previous;
  UINT32  Current;
  UINTN   Retry;

  if (!Ps4RtcCounterRaw (Regs, &Previous)) {
    return FALSE;
  }

  for (Retry = 0; Retry < PS4_RTC_COUNTER_RETRIES; Retry++) {
    MicroSecondDelay (PS4_RTC_COUNTER_RETRY_DELAY);
    if (!Ps4RtcCounterRaw (Regs, &Current)) {
      return FALSE;
    }

    if (Current == Previous) {
      *Value = Current;
      return TRUE;
    }

    Previous = Current;
  }

  return FALSE;
}

#endif
