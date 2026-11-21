/** @file
  Ps4GopDxe - Graphics Output on the PS4, WITHOUT PCI enumeration.

  The scanout surface lives in DRAM (HW runs 2/3: CRTC1 surface 0x6e800000 / 0x6e008000), not in
  any GPU BAR, and the directboot stage has already switched the live display pipe to a linear
  surface and described it in gEfiGraphicsInfoHobGuid.

  HW run 07:21 UTC (2026-11-20): EDK II reached PciBusDxe and died while it enumerated the live GPU
  (00:01.0: "PciBus: Discovered PCI @ [00|01|00]", BAR lines, then black screen / no signal). PciBus
  sizes every BAR (writes all-ones) and toggles the command register of devices OrbisOS left running;
  the same class of change (Bus Master off on live devices) powered the console off at 06:08. So
  PciBusDxe is no longer in the firmware image, and this driver no longer binds to a PciIo handle:
  at load it installs GOP on its own handle (vendor device path) and adds that path to ConOut and
  ErrOut, because upstream PlatformConsole only adds PCI VGA devices.
**/

#include <PiDxe.h>
#include <Guid/GraphicsInfoHob.h>
#include <Guid/GlobalVariable.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

#define PS4_GOP_SIGNATURE  SIGNATURE_32 ('P', '4', 'G', 'P')

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} PS4_GOP_DEVICE_PATH;
#pragma pack()

typedef struct {
  UINT32                                  Signature;
  EFI_HANDLE                              Handle;
  PS4_GOP_DEVICE_PATH                     DevicePath;
  EFI_GRAPHICS_OUTPUT_PROTOCOL            Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE       Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    Info;
  FRAME_BUFFER_CONFIGURE                  *BltConfig;
  UINTN                                   BltConfigSize;
} PS4_GOP_PRIVATE;

#define PS4_GOP_FROM_GOP(a)  (CR (a, PS4_GOP_PRIVATE, Gop, PS4_GOP_SIGNATURE))   // CR() has no outer parens

STATIC
EFI_STATUS
EFIAPI
Ps4GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  PS4_GOP_PRIVATE  *Private;

  if ((This == NULL) || (SizeOfInfo == NULL) || (Info == NULL) || (ModeNumber >= This->Mode->MaxMode)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = PS4_GOP_FROM_GOP (This);
  *Info   = AllocateCopyPool (sizeof (Private->Info), &Private->Info);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (Private->Info);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ps4GopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  PS4_GOP_PRIVATE                *Private;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;

  if (ModeNumber >= This->Mode->MaxMode) {
    return EFI_UNSUPPORTED;
  }

  Private = PS4_GOP_FROM_GOP (This);
  ZeroMem (&Black, sizeof (Black));
  return FrameBufferBlt (
           Private->BltConfig,
           &Black,
           EfiBltVideoFill,
           0,
           0,
           0,
           0,
           Private->Info.HorizontalResolution,
           Private->Info.VerticalResolution,
           0
           );
}

STATIC
EFI_STATUS
EFIAPI
Ps4GopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta
  )
{
  EFI_TPL     OldTpl;
  EFI_STATUS  Status;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  Status = FrameBufferBlt (
             PS4_GOP_FROM_GOP (This)->BltConfig,
             BltBuffer,
             BltOperation,
             SourceX,
             SourceY,
             DestinationX,
             DestinationY,
             Width,
             Height,
             Delta
             );
  gBS->RestoreTPL (OldTpl);
  return Status;
}

/* Append Path as one more instance of console variable Name (ConOut / ErrOut) unless present. */
STATIC
VOID
AddToConsoleVariable (
  IN CHAR16                    *Name,
  IN EFI_DEVICE_PATH_PROTOCOL  *Path
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Old;
  EFI_DEVICE_PATH_PROTOCOL  *New;
  UINTN                     OldSize;
  UINTN                     PathSize;
  UINTN                     Offset;
  EFI_STATUS                Status;

  Old      = NULL;
  OldSize  = 0;
  PathSize = GetDevicePathSize (Path) - END_DEVICE_PATH_LENGTH;
  Status   = GetVariable2 (Name, &gEfiGlobalVariableGuid, (VOID **)&Old, &OldSize);
  if (EFI_ERROR (Status)) {
    Old = NULL;
  } else if (Old != NULL) {
    for (Offset = 0; Offset + PathSize <= OldSize; Offset++) {
      if (CompareMem ((UINT8 *)Old + Offset, Path, PathSize) == 0) {
        FreePool (Old);
        return;                                   // already there
      }
    }
  }

  New = AppendDevicePathInstance (Old, Path);
  if (Old != NULL) {
    FreePool (Old);
  }

  if (New == NULL) {
    return;
  }

  Status = gRT->SetVariable (
                  Name,
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  GetDevicePathSize (New),
                  New
                  );
  DEBUG ((DEBUG_INFO, "Ps4GopDxe: %s += vendor GOP path: %r\n", Name, Status));
  FreePool (New);
}

EFI_STATUS
EFIAPI
Ps4GopEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  VOID                             *Hob;
  CONST EFI_PEI_GRAPHICS_INFO_HOB  *Gfx;
  PS4_GOP_PRIVATE                  *Private;
  EFI_STATUS                       Status;

  Hob = GetFirstGuidHob (&gEfiGraphicsInfoHobGuid);
  if (Hob == NULL) {
    DEBUG ((DEBUG_ERROR, "Ps4GopDxe: no graphics info HOB, not installing\n"));
    return EFI_NOT_FOUND;
  }

  Gfx = GET_GUID_HOB_DATA (Hob);
  if ((Gfx->FrameBufferBase == 0) || (Gfx->GraphicsMode.HorizontalResolution == 0) ||
      (Gfx->GraphicsMode.VerticalResolution == 0))
  {
    return EFI_NOT_FOUND;
  }

  Private = AllocateZeroPool (sizeof (*Private));
  if (Private == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Private->Signature = PS4_GOP_SIGNATURE;
  CopyMem (&Private->Info, &Gfx->GraphicsMode, sizeof (Private->Info));
  Private->Mode.MaxMode         = 1;
  Private->Mode.Mode            = 0;
  Private->Mode.Info            = &Private->Info;
  Private->Mode.SizeOfInfo      = sizeof (Private->Info);
  Private->Mode.FrameBufferBase = Gfx->FrameBufferBase;
  Private->Mode.FrameBufferSize = Gfx->FrameBufferSize;
  Private->Gop.QueryMode        = Ps4GopQueryMode;
  Private->Gop.SetMode          = Ps4GopSetMode;
  Private->Gop.Blt              = Ps4GopBlt;
  Private->Gop.Mode             = &Private->Mode;

  Private->BltConfigSize = 0;
  Status                 = FrameBufferBltConfigure (
                             (VOID *)(UINTN)Gfx->FrameBufferBase,
                             &Private->Info,
                             Private->BltConfig,
                             &Private->BltConfigSize
                             );
  if (Status == RETURN_BUFFER_TOO_SMALL) {
    Private->BltConfig = AllocatePool (Private->BltConfigSize);
    if (Private->BltConfig == NULL) {
      FreePool (Private);
      return EFI_OUT_OF_RESOURCES;
    }

    Status = FrameBufferBltConfigure (
               (VOID *)(UINTN)Gfx->FrameBufferBase,
               &Private->Info,
               Private->BltConfig,
               &Private->BltConfigSize
               );
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Ps4GopDxe: FrameBufferBltConfigure: %r\n", Status));
    if (Private->BltConfig != NULL) {
      FreePool (Private->BltConfig);
    }

    FreePool (Private);
    return Status;
  }

  Private->DevicePath.Vendor.Header.Type    = HARDWARE_DEVICE_PATH;
  Private->DevicePath.Vendor.Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&Private->DevicePath.Vendor.Header, sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Private->DevicePath.Vendor.Guid, &gPs4GopDevicePathGuid);
  SetDevicePathEndNode (&Private->DevicePath.End);

  Private->Handle = NULL;
  Status          = gBS->InstallMultipleProtocolInterfaces (
                           &Private->Handle,
                           &gEfiDevicePathProtocolGuid,
                           &Private->DevicePath,
                           &gEfiGraphicsOutputProtocolGuid,
                           &Private->Gop,
                           NULL
                           );
  if (EFI_ERROR (Status)) {
    FreePool (Private->BltConfig);
    FreePool (Private);
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "Ps4GopDxe: GOP %ux%u stride %u at 0x%lx (no PCI)\n",
    Private->Info.HorizontalResolution,
    Private->Info.VerticalResolution,
    Private->Info.PixelsPerScanLine,
    Private->Mode.FrameBufferBase
    ));

  AddToConsoleVariable (L"ConOut", (EFI_DEVICE_PATH_PROTOCOL *)&Private->DevicePath);
  AddToConsoleVariable (L"ErrOut", (EFI_DEVICE_PATH_PROTOCOL *)&Private->DevicePath);
  return EFI_SUCCESS;
}
