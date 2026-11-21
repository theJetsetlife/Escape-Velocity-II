/** @file
  ResetSystemLib for the PS4 Fat.

  Upstream UefiPayloadPkg ResetSystemLib needs the FADT reset register; Sony's FADT has none
  (ResetReg.Address = 0, HW run 20:14 UTC), so it ASSERTs at load (HW run 22:18 UTC). There is
  no 0xCF9 either: reset and power-off are requests to syscon over ICC, which this firmware
  does not drive yet. Until then a reset request is reported on screen and the CPU is parked.
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/ResetSystemLib.h>

STATIC
VOID
Park (
  IN CONST CHAR8  *What
  )
{
  DEBUG ((DEBUG_ERROR, "PS4: %a requested - no ICC reset yet; hold the power button to turn off\n", What));
  CpuDeadLoop ();
}

VOID
EFIAPI
ResetCold (
  VOID
  )
{
  Park ("cold reset");
}

VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  Park ("warm reset");
}

VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  Park ("shutdown");
}

VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  Park ("platform-specific reset");
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetWarm:
      ResetWarm ();
      break;
    case EfiResetShutdown:
      ResetShutdown ();
      break;
    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (DataSize, ResetData);
      break;
    default:
      ResetCold ();
      break;
  }
}
