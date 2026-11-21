/** @file
  Ps4LogDxe - one-command logging for the PS4 firmware.

  Shell command:  ps4log [-o fsN:] [-dns <host name>] [-nonet]
    Writes ONE file \ps4log\YYYYMMDD-HHMMSS.txt on FS0: (or -o) containing
      1. the whole boot log (ring the directboot stage published; every DEBUG line),
      2. a hardware dump (firmware, CPU/MSRs/LAPIC, UEFI memory map, ACPI, PCI bus 0, southbridge
         MSI block, GOP / block / file-system / USB / NIC devices),
      3. network tests per NIC: link, DHCP lease, DNS lookup (-dns, default example.com), PXE discover +
         TFTP size probe of the offered boot file, netboot entries (skipped with -nonet).
  Auto-save: at ReadyToBoot (once), sections 1 + 2 go to \ps4log\boot-YYYYMMDD-HHMMSS.txt on the first
  writable file system reached through USB (network tests only on demand: they take seconds).
**/

#include <PiDxe.h>
#include <Guid/EventGroup.h>
#include <Guid/FileInfo.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellDynamicCommand.h>
#include <Protocol/ShellParameters.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "Ps4Log.h"

STATIC BOOLEAN  mAutoSaved;

/* \ps4log\<Prefix>YYYYMMDD-HHMMSS.txt (counter fallback when the RTC fails) */
STATIC
VOID
MakeName (
  OUT CHAR16        *Name,
  IN  UINTN         Size,
  IN  CONST CHAR16  *Prefix
  )
{
  EFI_TIME      T;
  STATIC UINTN  Counter;

  if (!EFI_ERROR (gRT->GetTime (&T, NULL))) {
    UnicodeSPrint (Name, Size, L"\\ps4log\\%s%04u%02u%02u-%02u%02u%02u.txt", Prefix, T.Year, T.Month, T.Day, T.Hour, T.Minute, T.Second);
  } else {
    UnicodeSPrint (Name, Size, L"\\ps4log\\%slog%04u.txt", Prefix, Counter++);
  }
}

STATIC
EFI_STATUS
WriteReport (
  IN  EFI_HANDLE    FsHandle,
  IN  CONST CHAR16  *Prefix,
  IN  REPORT        *R,
  OUT CHAR16        *NameOut,
  IN  UINTN         NameSize
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_BLOCK_IO_PROTOCOL            *Blk;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *Dir;
  EFI_FILE_PROTOCOL                *File;
  UINTN                            Size;
  EFI_STATUS                       Status;

  if (EFI_ERROR (gBS->HandleProtocol (FsHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
    return EFI_UNSUPPORTED;
  }

  if (!EFI_ERROR (gBS->HandleProtocol (FsHandle, &gEfiBlockIoProtocolGuid, (VOID **)&Blk)) && Blk->Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (Root, &Dir, L"\\ps4log", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  if (!EFI_ERROR (Status)) {
    Dir->Close (Dir);
  }

  MakeName (NameOut, NameSize, Prefix);
  Status = Root->Open (Root, &File, NameOut, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (!EFI_ERROR (Status)) {
    Size   = R->Len;
    Status = File->Write (File, &Size, R->Buf);
    if (!EFI_ERROR (Status)) {
      Status = File->Flush (File);
    }

    File->Close (File);
  }

  Root->Close (Root);
  return Status;
}

//
// ---- Shell command ----
//
STATIC
SHELL_STATUS
EFIAPI
Ps4LogCommand (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN EFI_SYSTEM_TABLE                    *SystemTable,
  IN EFI_SHELL_PARAMETERS_PROTOCOL       *Params,
  IN EFI_SHELL_PROTOCOL                  *Shell
  )
{
  CONST CHAR16                    *Target;
  CONST CHAR16                    *DnsName;
  BOOLEAN                         Net;
  UINTN                           I;
  CONST EFI_DEVICE_PATH_PROTOCOL  *MapDp;
  EFI_DEVICE_PATH_PROTOCOL        *Dp;
  EFI_HANDLE                      FsHandle;
  REPORT                          R;
  CHAR16                          Name[64];
  EFI_STATUS                      Status;

  Target  = L"FS0:";
  DnsName = L"example.com";
  Net     = TRUE;
  for (I = 1; I < Params->Argc; I++) {
    if ((StrCmp (Params->Argv[I], L"-o") == 0) && (I + 1 < Params->Argc)) {
      Target = Params->Argv[++I];
    } else if ((StrCmp (Params->Argv[I], L"-dns") == 0) && (I + 1 < Params->Argc)) {
      DnsName = Params->Argv[++I];
    } else if (StrCmp (Params->Argv[I], L"-nonet") == 0) {
      Net = FALSE;
    } else {
      Print (L"usage: ps4log [-o fsN:] [-dns <host name>] [-nonet]\n");
      return SHELL_INVALID_PARAMETER;
    }
  }

  MapDp = Shell->GetDevicePathFromMap (Target);
  if (MapDp == NULL) {
    Print (L"ps4log: %s is not mapped (plug in a FAT USB stick, then 'map -r')\n", Target);
    return SHELL_NOT_FOUND;
  }

  Dp     = (EFI_DEVICE_PATH_PROTOCOL *)MapDp;
  Status = gBS->LocateDevicePath (&gEfiSimpleFileSystemProtocolGuid, &Dp, &FsHandle);
  if (EFI_ERROR (Status)) {
    Print (L"ps4log: %s is not a file system: %r\n", Target, Status);
    return SHELL_NOT_FOUND;
  }

  ZeroMem (&R, sizeof (R));
  R.Echo = TRUE;
  RPrint (&R, "ps4log report (Ps4LogDxe)\n");
  ReportBootLog (&R);
  ReportHardware (&R);
  if (Net) {
    ReportNetwork (&R, DnsName);
  }

  Status = WriteReport (FsHandle, L"", &R, Name, sizeof (Name));
  if (EFI_ERROR (Status)) {
    Print (L"ps4log: writing to %s failed: %r%s\n", Target, Status,
           (Status == EFI_WRITE_PROTECTED) ? L" (read-only volume; NTFS is read-only here, use a FAT stick)" : L"");
  } else {
    Print (L"ps4log: wrote %s%s (%u bytes)\n", Target, Name, R.Len);
  }

  if (R.Buf != NULL) {
    FreePool (R.Buf);
  }

  return EFI_ERROR (Status) ? SHELL_DEVICE_ERROR : SHELL_SUCCESS;
}

STATIC
CHAR16 *
EFIAPI
Ps4LogHelp (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN CONST CHAR8                         *Language
  )
{
  return AllocateCopyPool (
           sizeof (L"ps4log [-o fsN:] [-dns <host name>] [-nonet]\r\n  Saves boot log, hardware dump and LAN/DHCP/DNS/PXE tests to fsN:\\ps4log\\<time>.txt (default FS0:).\r\n"),
           L"ps4log [-o fsN:] [-dns <host name>] [-nonet]\r\n  Saves boot log, hardware dump and LAN/DHCP/DNS/PXE tests to fsN:\\ps4log\\<time>.txt (default FS0:).\r\n"
           );
}

STATIC EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  mPs4LogCommand = { L"ps4log", Ps4LogCommand, Ps4LogHelp };

//
// ---- auto-save at ReadyToBoot ----
//
STATIC
BOOLEAN
IsUsbPath (
  IN EFI_DEVICE_PATH_PROTOCOL  *Dp
  )
{
  for ( ; Dp != NULL && !IsDevicePathEnd (Dp); Dp = NextDevicePathNode (Dp)) {
    if ((DevicePathType (Dp) == MESSAGING_DEVICE_PATH) &&
        ((DevicePathSubType (Dp) == MSG_USB_DP) || (DevicePathSubType (Dp) == MSG_USB_CLASS_DP)))
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
EFIAPI
AutoSave (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE  *H;
  UINTN       N;
  UINTN       I;
  REPORT      R;
  CHAR16      Name[64];
  EFI_STATUS  Status;

  if (mAutoSaved) {
    return;
  }

  mAutoSaved = TRUE;
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &N, &H))) {
    DEBUG ((DEBUG_INFO, "ps4log: auto-save skipped (no file system)\n"));
    return;
  }

  ZeroMem (&R, sizeof (R));
  RPrint (&R, "ps4log auto-save at ReadyToBoot (Ps4LogDxe)\n");
  ReportBootLog (&R);
  ReportHardware (&R);
  Status = EFI_NOT_FOUND;
  for (I = 0; I < N; I++) {
    if (!IsUsbPath (DevicePathFromHandle (H[I]))) {
      continue;
    }

    Status = WriteReport (H[I], L"boot-", &R, Name, sizeof (Name));
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  DEBUG ((DEBUG_INFO, "ps4log: auto-save %s: %r\n", EFI_ERROR (Status) ? L"(no writable USB volume)" : Name, Status));
  FreePool (H);
  if (R.Buf != NULL) {
    FreePool (R.Buf);
  }
}

EFI_STATUS
EFIAPI
Ps4LogEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT   Event;
  EFI_STATUS  Status;

  Status = gBS->InstallProtocolInterface (&ImageHandle, &gEfiShellDynamicCommandProtocolGuid, EFI_NATIVE_INTERFACE, &mPs4LogCommand);
  DEBUG ((DEBUG_INFO, "ps4log: Shell command installed: %r\n", Status));
  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, AutoSave, NULL, &gEfiEventReadyToBootGuid, &Event);
  return Status;
}
