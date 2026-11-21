/** @file
  Ps4LogDxe - report buffer, boot-log ring dump and hardware dump. Everything here only READS
  hardware state (PCI config, MSRs, LAPIC, the Aeolia MSI block); nothing is written to devices.
**/

#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/Acpi.h>
#include <Guid/AcpiBoardInfoGuid.h>
#include <Guid/SmBios.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SerialIo.h>
#include <Protocol/UsbIo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/Ps4UartLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/Ps4DirectbootInfo.h>
#include <Ps4FbConsole.h>
#include "Ps4Log.h"

//
// ---- report buffer ----
//
STATIC
BOOLEAN
Grow (
  IN OUT REPORT  *R,
  IN     UINTN   Extra
  )
{
  CHAR8  *New;
  UINTN  Cap;

  if (R->Len + Extra + 1 <= R->Cap) {
    return TRUE;
  }

  Cap = (R->Cap == 0) ? SIZE_64KB : R->Cap;
  while (Cap < R->Len + Extra + 1) {
    Cap *= 2;
  }

  New = AllocatePool (Cap);
  if (New == NULL) {
    return FALSE;
  }

  if (R->Buf != NULL) {
    CopyMem (New, R->Buf, R->Len);
    FreePool (R->Buf);
  }

  R->Buf = New;
  R->Cap = Cap;
  return TRUE;
}

VOID
RAppend (
  IN OUT REPORT      *R,
  IN     CONST CHAR8 *Text,
  IN     UINTN       Length
  )
{
  UINTN  I;

  if (!Grow (R, Length * 2)) {
    return;
  }

  for (I = 0; I < Length; I++) {
    if ((Text[I] == '\n') && ((R->Len == 0) || (R->Buf[R->Len - 1] != '\r'))) {
      R->Buf[R->Len++] = '\r';
    }

    R->Buf[R->Len++] = Text[I];
  }

  R->Buf[R->Len] = 0;
}

VOID
EFIAPI
RPrint (
  IN OUT REPORT      *R,
  IN     CONST CHAR8 *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR8    Line[512];
  UINTN    N;

  VA_START (Marker, Format);
  N = AsciiVSPrint (Line, sizeof (Line), Format, Marker);
  VA_END (Marker);
  RAppend (R, Line, N);
}

VOID
RSection (
  IN OUT REPORT      *R,
  IN     CONST CHAR8 *Title
  )
{
  RPrint (R, "\n==================== %a ====================\n", Title);
  if (R->Echo) {
    Print (L"ps4log: %a\n", Title);
  }
}

VOID
RHexDump32 (
  IN OUT REPORT  *R,
  IN     UINTN   Address,
  IN     UINTN   Length
  )
{
  UINTN  Off;

  for (Off = 0; Off < Length; Off += 16) {
    RPrint (
      R,
      "  %08lx: %08x %08x %08x %08x\n",
      (UINT64)(Address + Off),
      MmioRead32 (Address + Off),
      MmioRead32 (Address + Off + 4),
      MmioRead32 (Address + Off + 8),
      MmioRead32 (Address + Off + 12)
      );
  }
}

//
// ---- 1. boot log (ring the stage published in the fbcon descriptor v3) ----
//
VOID
ReportBootLog (
  IN OUT REPORT  *R
  )
{
  PS4_FBCON       *C;
  PS4_LOG_HEADER  *H;
  CONST CHAR8     *D;
  UINT64          Start;
  UINT64          Written;
  UINTN           First;
  UINTN           Count;

  RSection (R, "boot log (every DEBUG line since the stage took over)");
  C = (PS4_FBCON *)(UINTN)PS4_FBCON_ADDRESS;
  if ((C->Magic != PS4_FBCON_MAGIC) || (C->Version < 3) || (C->LogBase == 0)) {
    RPrint (R, "no boot-log ring (fbcon magic %x version %u)\n", C->Magic, C->Version);
    return;
  }

  H = (PS4_LOG_HEADER *)(UINTN)C->LogBase;
  if ((H->Magic != PS4_LOG_MAGIC) || (H->Size == 0) || (H->Size != C->LogSize)) {
    RPrint (R, "boot-log ring at %lx invalid (magic %x size %x)\n", C->LogBase, H->Magic, H->Size);
    return;
  }

  Written = H->Written;                               // snapshot (this command keeps logging)
  D       = (CONST CHAR8 *)(H + 1);
  Start   = (Written > H->Size) ? Written - H->Size : 0;
  if (Start != 0) {
    RPrint (R, "[ring wrapped: the first %lu bytes of the boot are lost]\n", Start);
  }

  First = (UINTN)(Start % H->Size);
  Count = (UINTN)(Written - Start);
  if (First + Count <= H->Size) {
    RAppend (R, D + First, Count);
  } else {
    RAppend (R, D + First, H->Size - First);
    RAppend (R, D, Count - (H->Size - First));
  }

  RPrint (R, "\n[boot log: %lu bytes total, ring %u bytes at %lx]\n", Written, H->Size, C->LogBase);
}

//
// ---- 2. hardware ----
//
STATIC CONST CHAR8  *mMemType[] = {
  "Reserved", "LoaderCode", "LoaderData", "BS_Code", "BS_Data", "RT_Code", "RT_Data", "Available",
  "Unusable", "ACPI_Recl", "ACPI_NVS", "MMIO", "MMIO_Port", "PalCode", "Persistent", "Unaccepted"
};

STATIC
VOID
ReportFirmware (
  IN OUT REPORT  *R
  )
{
  EFI_TIME                   Now;
  VOID                       *Hob;
  CONST PS4_DIRECTBOOT_INFO  *Info;

  RSection (R, "firmware");
  RPrint (R, "vendor %s revision %x, UEFI %u.%u\n", gST->FirmwareVendor, gST->FirmwareRevision,
          gST->Hdr.Revision >> 16, (gST->Hdr.Revision & 0xFFFF) / 10);
  if (!EFI_ERROR (gRT->GetTime (&Now, NULL))) {
    RPrint (R, "time %04u-%02u-%02u %02u:%02u:%02u\n", Now.Year, Now.Month, Now.Day, Now.Hour, Now.Minute, Now.Second);
  }

  Hob = GetFirstGuidHob (&gPs4DirectbootInfoGuid);
  if (Hob != NULL) {
    Info = GET_GUID_HOB_DATA (Hob);
    RPrint (R, "directboot: unix time %lu, TSC %lu Hz, kbase %lx, fb %lx %ux%u pitch %u crtc %u, halted APs %u\n",
            Info->UnixTime, Info->TscHz, Info->KernelBase, Info->FbBase, Info->FbWidth, Info->FbHeight,
            Info->FbPitchBytes, Info->FbCrtc, Info->HaltedAps);
  }

  RPrint (R, "southbridge UART: %a\n", Ps4UartPresent () ? "present (115200 8N1)" : "not found");
}

STATIC
VOID
ReportCpu (
  IN OUT REPORT  *R
  )
{
  UINT32  Regs[4];
  UINT32  Brand[13];
  UINT32  Max;
  UINT32  Ext;
  UINT32  Family;
  UINT32  Ecx1;
  UINT32  Ecx8;
  UINT32  Edx8;
  UINT64  ApicBase;
  UINTN   Lapic;
  UINTN   I;
  BOOLEAN Amd;

  RSection (R, "cpu");
  AsmCpuid (0, &Max, &Regs[0], &Regs[2], &Regs[1]);
  Regs[3] = 0;
  RPrint (R, "vendor %.12a, max leaf %x\n", (CHAR8 *)Regs, Max);
  Amd = (Regs[0] == 0x68747541);
  AsmCpuid (1, &Regs[0], &Regs[1], &Ecx1, &Regs[3]);
  Family = (Regs[0] >> 8) & 0xF;
  if (Family == 0xF) {
    Family += (Regs[0] >> 20) & 0xFF;
  }

  RPrint (R, "family %xh model %x stepping %x, cpuid1 ecx %08x edx %08x\n", Family,
          ((Regs[0] >> 4) & 0xF) | ((Regs[0] >> 12) & 0xF0), Regs[0] & 0xF, Ecx1, Regs[3]);
  AsmCpuid (0x80000000, &Ext, NULL, NULL, NULL);
  if (Ext >= 0x80000004) {
    ZeroMem (Brand, sizeof (Brand));
    AsmCpuid (0x80000002, &Brand[0], &Brand[1], &Brand[2], &Brand[3]);
    AsmCpuid (0x80000003, &Brand[4], &Brand[5], &Brand[6], &Brand[7]);
    AsmCpuid (0x80000004, &Brand[8], &Brand[9], &Brand[10], &Brand[11]);
    RPrint (R, "brand %a\n", (CHAR8 *)Brand);
  }

  Ecx8 = 0;
  Edx8 = 0;
  if (Ext >= 0x80000001) {
    AsmCpuid (0x80000001, NULL, NULL, &Ecx8, &Edx8);
  }

  RPrint (R, "features: SVM %u  LM %u  NX %u  RDRAND %u  AES %u  AVX %u  x2APIC %u\n",
          (Ecx8 >> 2) & 1, (Edx8 >> 29) & 1, (Edx8 >> 20) & 1, (Ecx1 >> 30) & 1, (Ecx1 >> 25) & 1, (Ecx1 >> 28) & 1, (Ecx1 >> 21) & 1);
  if (Amd && (Family == 0x16)) {                       // PS4 (Jaguar): AMD memory MSRs
    RPrint (R, "SYSCFG %lx TOP_MEM %lx TOM2 %lx\n", AsmReadMsr64 (0xC0010010), AsmReadMsr64 (0xC001001A), AsmReadMsr64 (0xC001001D));
    if ((Ecx8 & BIT2) != 0) {
      RPrint (R, "VM_CR %lx (SVMDIS bit 4 = %u)\n", AsmReadMsr64 (0xC0010114), (UINT32)(AsmReadMsr64 (0xC0010114) >> 4) & 1);
    }
  }

  ApicBase = AsmReadMsr64 (0x1B);
  Lapic    = (UINTN)(ApicBase & 0xFFFFFF000ULL);
  RPrint (R, "APIC_BASE %lx: ID %08x VER %08x TPR %x PPR %x SVR %x LVT-timer %08x\n", ApicBase,
          MmioRead32 (Lapic + 0x20), MmioRead32 (Lapic + 0x30), MmioRead32 (Lapic + 0x80),
          MmioRead32 (Lapic + 0xA0), MmioRead32 (Lapic + 0xF0), MmioRead32 (Lapic + 0x320));
  for (I = 0; I < 8; I++) {
    if ((MmioRead32 (Lapic + 0x100 + I * 0x10) | MmioRead32 (Lapic + 0x200 + I * 0x10)) != 0) {
      RPrint (R, "  vectors %02x-%02x: ISR %08x IRR %08x\n", I * 32, I * 32 + 31,
              MmioRead32 (Lapic + 0x100 + I * 0x10), MmioRead32 (Lapic + 0x200 + I * 0x10));
    }
  }
}

STATIC
VOID
ReportMemoryMap (
  IN OUT REPORT  *R
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVer;
  UINT64                 Pages[16];
  UINTN                  I;
  EFI_STATUS             Status;

  RSection (R, "UEFI memory map");
  Size   = 0;
  Map    = NULL;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    RPrint (R, "GetMemoryMap: %r\n", Status);
    return;
  }

  Size += 8 * DescSize;
  Map   = AllocatePool (Size);
  if ((Map == NULL) || EFI_ERROR (gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer))) {
    RPrint (R, "GetMemoryMap failed\n");
    if (Map != NULL) {
      FreePool (Map);
    }

    return;
  }

  ZeroMem (Pages, sizeof (Pages));
  for (D = Map; (UINT8 *)D < (UINT8 *)Map + Size; D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)D + DescSize)) {
    RPrint (R, "  %-10a %016lx-%016lx %10lu pages attr %lx\n", (D->Type < 16) ? mMemType[D->Type] : "?",
            D->PhysicalStart, D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages) - 1, D->NumberOfPages, D->Attribute);
    if (D->Type < 16) {
      Pages[D->Type] += D->NumberOfPages;
    }
  }

  for (I = 0; I < 16; I++) {
    if (Pages[I] != 0) {
      RPrint (R, "total %-10a %10lu pages = %lu MiB\n", mMemType[I], Pages[I], Pages[I] / 256);
    }
  }

  RPrint (R, "usable by an OS loader (Available + BS + Loader): %lu MiB\n",
          (Pages[EfiConventionalMemory] + Pages[EfiBootServicesCode] + Pages[EfiBootServicesData] +
           Pages[EfiLoaderCode] + Pages[EfiLoaderData]) / 256);
  FreePool (Map);
}

STATIC
CONST CHAR8 *
MemTypeAt (
  IN UINT64  Address
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVer;
  CONST CHAR8            *Type;

  Size = 0;
  Map  = NULL;
  if (gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer) != EFI_BUFFER_TOO_SMALL) {
    return "?";
  }

  Size += 8 * DescSize;
  Map   = AllocatePool (Size);
  if ((Map == NULL) || EFI_ERROR (gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer))) {
    if (Map != NULL) {
      FreePool (Map);
    }

    return "?";
  }

  Type = "NOT IN MAP";
  for (D = Map; (UINT8 *)D < (UINT8 *)Map + Size; D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)D + DescSize)) {
    if ((Address >= D->PhysicalStart) && (Address < D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages))) {
      Type = (D->Type < 16) ? mMemType[D->Type] : "?";
      break;
    }
  }

  FreePool (Map);
  return Type;
}

STATIC
VOID
ReportAcpi (
  IN OUT REPORT  *R
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                   *T;
  UINTN                                         N;
  UINTN                                         I;
  UINT8                                         *E;
  UINT8                                         *End;
  EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE     *F;

  RSection (R, "ACPI");
  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp)) || (Rsdp == NULL)) {
    RPrint (R, "no ACPI 2.0 RSDP\n");
    return;
  }

  RPrint (R, "RSDP %p (%a) rev %u OEM %.6a RSDT %x XSDT %lx (%a)\n", Rsdp, MemTypeAt ((UINTN)Rsdp), Rsdp->Revision,
          Rsdp->OemId, Rsdp->RsdtAddress, Rsdp->XsdtAddress, MemTypeAt (Rsdp->XsdtAddress));
  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  if (Xsdt == NULL) {
    return;
  }

  N = (Xsdt->Length - sizeof (*Xsdt)) / 8;
  for (I = 0; I < N; I++) {
    T = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)ReadUnaligned64 ((UINT64 *)((UINT8 *)(Xsdt + 1) + I * 8));
    if (T == NULL) {
      continue;
    }

    RPrint (R, "  %.4a at %p (%a) len %u rev %u OEM %.6a %.8a\n", (CHAR8 *)&T->Signature, T, MemTypeAt ((UINTN)T),
            T->Length, T->Revision, T->OemId, (CHAR8 *)&T->OemTableId);
    if (T->Signature == EFI_ACPI_2_0_MULTIPLE_SAPIC_DESCRIPTION_TABLE_SIGNATURE) {    // "APIC"
      E   = (UINT8 *)T + sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 8;
      End = (UINT8 *)T + T->Length;
      RPrint (R, "    LAPIC address %x flags %x\n", *(UINT32 *)((UINT8 *)T + 36), *(UINT32 *)((UINT8 *)T + 40));
      while ((E + 2 <= End) && (E[1] >= 2)) {
        switch (E[0]) {
          case 0: RPrint (R, "    LAPIC  uid %u id %u flags %x\n", E[2], E[3], *(UINT32 *)(E + 4)); break;
          case 1: RPrint (R, "    IOAPIC id %u addr %x gsi %u\n", E[2], *(UINT32 *)(E + 4), *(UINT32 *)(E + 8)); break;
          case 2: RPrint (R, "    ISO bus %u irq %u gsi %u flags %x\n", E[2], E[3], *(UINT32 *)(E + 4), *(UINT16 *)(E + 8)); break;
          case 4: RPrint (R, "    LAPIC-NMI uid %x flags %x lint %u\n", E[2], *(UINT16 *)(E + 3), E[5]); break;
          default: RPrint (R, "    entry type %u len %u\n", E[0], E[1]); break;
        }

        E += E[1];
      }
    } else if (T->Signature == EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {  // "FACP"
      F = (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE *)T;
      RPrint (R, "    flags %x (HW_REDUCED %u) IAPC_BOOT_ARCH %x SCI %u PM_TMR %x PM1a_EVT %x\n", F->Flags,
              (F->Flags >> 20) & 1, F->IaPcBootArch, F->SciInt, F->PmTmrBlk, F->Pm1aEvtBlk);
      RPrint (R, "    DSDT %x X_DSDT %lx (%a)  FACS %x X_FACS %lx (%a)\n", F->Dsdt, F->XDsdt,
              MemTypeAt ((F->XDsdt != 0) ? F->XDsdt : F->Dsdt), F->FirmwareCtrl, F->XFirmwareCtrl,
              MemTypeAt ((F->XFirmwareCtrl != 0) ? F->XFirmwareCtrl : F->FirmwareCtrl));
      if ((F->Dsdt != 0) && (F->XDsdt != 0) && (F->Dsdt != F->XDsdt)) {
        RPrint (R, "    WARNING: DSDT and X_DSDT differ\n");
      }

      if ((F->FirmwareCtrl != 0) && (F->XFirmwareCtrl != 0) && (F->FirmwareCtrl != F->XFirmwareCtrl)) {
        RPrint (R, "    WARNING: FACS and X_FACS differ\n");
      }
    }
  }
}

STATIC
UINTN
EcamBase (
  VOID
  )
{
  VOID  *Hob;

  Hob = GetFirstGuidHob (&gUefiAcpiBoardInfoGuid);
  return (Hob == NULL) ? 0 : (UINTN)((ACPI_BOARD_INFO *)GET_GUID_HOB_DATA (Hob))->PcieBaseAddress;
}

STATIC
VOID
ReportPci (
  IN OUT REPORT  *R
  )
{
  UINTN   Ecam;
  UINTN   Dev;
  UINTN   Fn;
  UINTN   Cfg;
  UINT32  Id;
  UINT32  Bar4Lo;
  UINT32  Bar4Hi;
  UINTN   Msi;

  RSection (R, "PCI bus 0 (config space READ only)");
  Ecam = EcamBase ();
  if (Ecam == 0) {
    RPrint (R, "no ECAM (no MCFG)\n");
    return;
  }

  RPrint (R, "ECAM %lx\n", (UINT64)Ecam);
  for (Dev = 0; Dev < 32; Dev++) {
    for (Fn = 0; Fn < 8; Fn++) {
      Cfg = Ecam + (Dev << 15) + (Fn << 12);
      Id  = MmioRead32 (Cfg);
      if (((Id & 0xFFFF) == 0xFFFF) || ((Id & 0xFFFF) == 0)) {
        if (Fn == 0) {
          break;
        }

        continue;
      }

      if (((Id & 0xFFFF) == 0x104D) && (Dev != 0x14)) {   // Aeolia answers on every device number
        break;
      }

      RPrint (R, "  00:%02x.%x %04x:%04x class %06x cmd %04x bars %08x %08x %08x %08x %08x %08x\n", Dev, Fn,
              Id & 0xFFFF, Id >> 16, MmioRead32 (Cfg + 8) >> 8, MmioRead32 (Cfg + 4) & 0xFFFF,
              MmioRead32 (Cfg + 0x10), MmioRead32 (Cfg + 0x14), MmioRead32 (Cfg + 0x18),
              MmioRead32 (Cfg + 0x1C), MmioRead32 (Cfg + 0x20), MmioRead32 (Cfg + 0x24));
      if ((Fn == 0) && ((MmioRead32 (Cfg + 0x0C) & (BIT7 << 16)) == 0)) {
        break;
      }
    }
  }

  Id = MmioRead32 (Ecam + (0x14 << 15) + (4 << 12));
  if ((Id == 0x90A1104D) || (Id == 0x90CC104D)) {           // Aeolia / Belize glue: MSI block
    Bar4Lo = MmioRead32 (Ecam + (0x14 << 15) + (4 << 12) + 0x20);
    Bar4Hi = MmioRead32 (Ecam + (0x14 << 15) + (4 << 12) + 0x24);
    Msi    = (UINTN)((((UINT64)Bar4Hi << 32) | (Bar4Lo & ~0xFU)) + 0x1C8400);
    RPrint (R, "southbridge MSI block (BAR4+0x1C8400):\n");
    RHexDump32 (R, Msi, 0x200);
  }
}

STATIC
VOID
ReportDevices (
  IN OUT REPORT  *R
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_HANDLE                    *H;
  UINTN                         N;
  UINTN                         I;
  CHAR16                        *Text;
  EFI_BLOCK_IO_PROTOCOL         *Blk;
  EFI_USB_IO_PROTOCOL           *Usb;
  EFI_USB_DEVICE_DESCRIPTOR     Dd;
  EFI_USB_INTERFACE_DESCRIPTOR  Id;
  EFI_SIMPLE_NETWORK_PROTOCOL   *Snp;
  VOID                          *Smbios;

  RSection (R, "devices");
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) {
    RPrint (R, "GOP %ux%u stride %u fb %lx size %lx\n", Gop->Mode->Info->HorizontalResolution,
            Gop->Mode->Info->VerticalResolution, Gop->Mode->Info->PixelsPerScanLine,
            Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
  }

  RPrint (R, "SMBIOS: %a\n",
          (!EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, &Smbios)) ||
           !EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiSmbiosTableGuid, &Smbios))) ? "present" : "none");

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &N, &H))) {
    for (I = 0; I < N; I++) {
      gBS->HandleProtocol (H[I], &gEfiBlockIoProtocolGuid, (VOID **)&Blk);
      Text = ConvertDevicePathToText (DevicePathFromHandle (H[I]), FALSE, FALSE);
      RPrint (R, "block: %a%a%a %lu x %u B  %s\n", Blk->Media->RemovableMedia ? "removable " : "",
              Blk->Media->LogicalPartition ? "partition " : "disk ", Blk->Media->ReadOnly ? "ro" : "rw",
              Blk->Media->LastBlock + 1, Blk->Media->BlockSize, (Text != NULL) ? Text : L"?");
      if (Text != NULL) {
        FreePool (Text);
      }
    }

    FreePool (H);
  }

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &N, &H))) {
    for (I = 0; I < N; I++) {
      Text = ConvertDevicePathToText (DevicePathFromHandle (H[I]), FALSE, FALSE);
      RPrint (R, "file system: %s\n", (Text != NULL) ? Text : L"?");
      if (Text != NULL) {
        FreePool (Text);
      }
    }

    FreePool (H);
  }

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &N, &H))) {
    for (I = 0; I < N; I++) {
      gBS->HandleProtocol (H[I], &gEfiUsbIoProtocolGuid, (VOID **)&Usb);
      if (!EFI_ERROR (Usb->UsbGetDeviceDescriptor (Usb, &Dd)) && !EFI_ERROR (Usb->UsbGetInterfaceDescriptor (Usb, &Id))) {
        RPrint (R, "usb: %04x:%04x interface class %02x/%02x/%02x\n", Dd.IdVendor, Dd.IdProduct,
                Id.InterfaceClass, Id.InterfaceSubClass, Id.InterfaceProtocol);
      }
    }

    FreePool (H);
  }

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleNetworkProtocolGuid, NULL, &N, &H))) {
    for (I = 0; I < N; I++) {
      gBS->HandleProtocol (H[I], &gEfiSimpleNetworkProtocolGuid, (VOID **)&Snp);
      RPrint (R, "nic: mac %02x:%02x:%02x:%02x:%02x:%02x state %u media %a\n",
              Snp->Mode->CurrentAddress.Addr[0], Snp->Mode->CurrentAddress.Addr[1], Snp->Mode->CurrentAddress.Addr[2],
              Snp->Mode->CurrentAddress.Addr[3], Snp->Mode->CurrentAddress.Addr[4], Snp->Mode->CurrentAddress.Addr[5],
              Snp->Mode->State, !Snp->Mode->MediaPresentSupported ? "unknown" : (Snp->Mode->MediaPresent ? "present" : "absent"));
    }

    FreePool (H);
  }

  N = 0;
  gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleTextInProtocolGuid, NULL, &N, &H);
  if (N != 0) {
    FreePool (H);
  }

  RPrint (R, "text input devices: %u\n", N);
  N = 0;
  gBS->LocateHandleBuffer (ByProtocol, &gEfiSerialIoProtocolGuid, NULL, &N, &H);
  if (N != 0) {
    FreePool (H);
  }

  RPrint (R, "serial ports: %u\n", N);
}

VOID
ReportHardware (
  IN OUT REPORT  *R
  )
{
  ReportFirmware (R);
  ReportCpu (R);
  ReportMemoryMap (R);
  ReportAcpi (R);
  ReportPci (R);
  ReportDevices (R);
}
