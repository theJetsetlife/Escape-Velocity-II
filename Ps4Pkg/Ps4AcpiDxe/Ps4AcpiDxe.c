/** @file
  Ps4AcpiDxe - make the ACPI tables handed over by the bootloader acceptable to OS loaders.

  DSDT: Sony's DSDT gives the PCI root bridge no bus range and no windows (_CRS = I/O 0xCF8 only), which
  Windows rejects. On Sony tables (or with PS4_DIRECTBOOT_FLAG_FORCE_PS4_DSDT from the QEMU harness) the DSDT
  in Ps4Dsdt.asl replaces it; its root-bridge windows are measured here: ECAM from MCFG, 32-bit windows from
  the top of low memory in the UEFI map to 0xFEBFFFFF around ECAM, 64-bit window from above the highest
  memory-map entry to the CPU physical limit (below AMD's HyperTransport hole). Every live BAR is checked
  against them (config space is only read).

  IOAPIC: Windows 10's HAL stops with HAL_INITIALIZATION_FAILED (0x5C, status C000000E) when the MADT lists
  no IOAPIC (QEMU with a Sony-shaped MADT, 2026-11-20). The PS4 has none and needs none (every device
  interrupt is an MSI), so when the MADT has no IOAPIC a RAM page stands in for one: a reserved page whose
  IOWIN (+0x10) is preloaded with 0x00170011 (version 0x11, 24 pins), added as IOAPIC ID 0 (the ID field its reads return), GSI base 0.
  Reads of ID/version see that value; later reads return what was last written (entries the OS masks).
  Same design as macos-ps4/docs/MADT-IOAPIC.md.

  FACS: AcpiTableDxe keeps a FACS that comes from the bootloader HOB IN PLACE (it assumes the bootloader
  put it in ACPI NVS memory described to the payload). On the PS4 (and in the QEMU harness) the FACS
  lives in memory the payload memory map does not describe, so Windows stops with ACPI_BIOS_ERROR
  (seen in QEMU 2026-11-20: FADT FIRMWARE_CTRL 0x7FFE0000 "not in map"). Here the FACS is copied into
  a 64-byte aligned ACPI NVS page below 4 GiB and the FADT is repointed (FIRMWARE_CTRL set,
  X_FIRMWARE_CTRL zero, checksum fixed).

  RTC: the PS4 has no PC-AT CMOS RTC at I/O 0x70/0x71, but Sony's FADT (revision 4, IAPC_BOOT_ARCH 0, HW run 2)
  says nothing, so every OS assumes one. On PS4 tables the FADT is rebuilt as revision 6 with IAPC_BOOT_ARCH
  "CMOS RTC not present" (BIT5), no RTC alarm/century CMOS indexes and no fixed RTC wake event, and the DSDT
  gets an ACPI Time and Alarm Device on the real clock (Ps4RtcDxe mailbox, Ps4Dsdt.asl \_SB.PTAD).
  The revision matters: Windows 10 22H2 HalpSetPlatformFlags reads IAPC_BOOT_ARCH only when the FADT revision
  is >= 5, and then answers time queries from the ACPI TAD (HalpQueryAcpiRealTimeClock) instead of CMOS
  (HalpReadCmosTime); HalpRtcDiscover likewise skips the CMOS RTC timer. Linux: no rtc_cmos device,
  wall clock from rtc-efi / acpi_tad. One firmware fix instead of a PS4 patch in each OS.
**/

#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/Acpi.h>
#include <Protocol/AcpiTable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <IndustryStandard/MemoryMappedConfigurationSpaceAccessTable.h>
#include <Guid/Ps4DirectbootInfo.h>
#include <Guid/Ps4RtcMailbox.h>
#include "Ps4Dsdt.h"

#define MMIO_TOP_32     0xFEBFFFFFull           // IOAPIC/HPET/LAPIC/flash region starts at 0xFEC00000
#define HT_HOLE_BASE    0xFD00000000ull         // AMD HyperTransport reserved range (1 TiB - 12 GiB)

/* UEFI memory type of the page holding Address, or EfiMaxMemoryType when it is not in the map. */
STATIC
EFI_MEMORY_TYPE
MemoryTypeAt (
  IN UINT64  Address
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVer;
  EFI_MEMORY_TYPE        Type;

  Size = 0;
  if (gBS->GetMemoryMap (&Size, NULL, &Key, &DescSize, &DescVer) != EFI_BUFFER_TOO_SMALL) {
    return EfiMaxMemoryType;
  }

  Size += 8 * DescSize;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    return EfiMaxMemoryType;
  }

  Type = EfiMaxMemoryType;
  if (!EFI_ERROR (gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer))) {
    for (D = Map; (UINT8 *)D < (UINT8 *)Map + Size; D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)D + DescSize)) {
      if ((Address >= D->PhysicalStart) && (Address < D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages))) {
        Type = (EFI_MEMORY_TYPE)D->Type;
        break;
      }
    }
  }

  FreePool (Map);
  return Type;
}

/* Highest end (exclusive) of memory-map entries in [Lo, Hi), MMIO types excluded. */
STATIC
UINT64
MapTop (
  IN UINT64  Lo,
  IN UINT64  Hi
  )
{
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINTN                  Size;
  UINTN                  Key;
  UINTN                  DescSize;
  UINT32                 DescVer;
  UINT64                 Top;
  UINT64                 End;

  Size = 0;
  if (gBS->GetMemoryMap (&Size, NULL, &Key, &DescSize, &DescVer) != EFI_BUFFER_TOO_SMALL) {
    return 0;
  }

  Size += 8 * DescSize;
  Map   = AllocatePool (Size);
  if (Map == NULL) {
    return 0;
  }

  Top = 0;
  if (!EFI_ERROR (gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &DescVer))) {
    for (D = Map; (UINT8 *)D < (UINT8 *)Map + Size; D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)D + DescSize)) {
      if ((D->Type == EfiMemoryMappedIO) || (D->Type == EfiMemoryMappedIOPortSpace)) {
        continue;
      }

      End = D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages);
      if ((D->PhysicalStart >= Lo) && (D->PhysicalStart < Hi) && (End > Top)) {
        Top = End;
      }
    }
  }

  FreePool (Map);
  return Top;
}

STATIC
EFI_ACPI_DESCRIPTION_HEADER *
FindTable (
  IN UINT32  Signature
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                   *T;
  UINTN                                         N;
  UINTN                                         I;

  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp)) || (Rsdp == NULL) ||
      (Rsdp->XsdtAddress == 0))
  {
    return NULL;
  }

  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  N    = (Xsdt->Length - sizeof (*Xsdt)) / sizeof (UINT64);
  for (I = 0; I < N; I++) {
    T = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)ReadUnaligned64 ((UINT64 *)(Xsdt + 1) + I);
    if ((T != NULL) && (T->Signature == Signature)) {
      return T;
    }
  }

  return NULL;
}

STATIC
EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE *
FindFadt (
  VOID
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  EFI_ACPI_DESCRIPTION_HEADER                   *T;
  UINTN                                         N;
  UINTN                                         I;

  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp)) || (Rsdp == NULL) ||
      (Rsdp->XsdtAddress == 0))
  {
    return NULL;
  }

  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  N    = (Xsdt->Length - sizeof (*Xsdt)) / sizeof (UINT64);
  for (I = 0; I < N; I++) {
    T = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)ReadUnaligned64 ((UINT64 *)(Xsdt + 1) + I);
    if ((T != NULL) && (T->Signature == EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE)) {
      return (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE *)T;
    }
  }

  return NULL;
}

STATIC
VOID
FixFacs (
  VOID
  )
{
  EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE     *Fadt;
  EFI_ACPI_2_0_FIRMWARE_ACPI_CONTROL_STRUCTURE  *Facs;
  EFI_PHYSICAL_ADDRESS                          New;
  UINT64                                        Old;
  EFI_STATUS                                    Status;

  Fadt = FindFadt ();
  if (Fadt == NULL) {
    DEBUG ((DEBUG_WARN, "Ps4Acpi: no FADT\n"));
    return;
  }

  Old = 0;
  if ((Fadt->Header.Length >= OFFSET_OF (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE, XDsdt)) && (Fadt->XFirmwareCtrl != 0)) {
    Old = Fadt->XFirmwareCtrl;
  } else {
    Old = Fadt->FirmwareCtrl;
  }

  if (Old == 0) {
    DEBUG ((DEBUG_WARN, "Ps4Acpi: FADT has no FACS (hardware-reduced?)\n"));
    return;
  }

  if (MemoryTypeAt (Old) == EfiACPIMemoryNVS) {
    DEBUG ((DEBUG_INFO, "Ps4Acpi: FACS %lx already in ACPI NVS\n", Old));
    return;
  }

  Facs = (EFI_ACPI_2_0_FIRMWARE_ACPI_CONTROL_STRUCTURE *)(UINTN)Old;
  if ((Facs->Signature != EFI_ACPI_2_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE) || (Facs->Length < 64) ||
      (Facs->Length > SIZE_4KB))
  {
    DEBUG ((DEBUG_ERROR, "Ps4Acpi: FACS at %lx invalid (sig %x len %u); left alone\n", Old, Facs->Signature, Facs->Length));
    return;
  }

  New    = BASE_4GB - 1;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiACPIMemoryNVS, 1, &New);    // page => 64-byte aligned
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Ps4Acpi: no NVS page for the FACS: %r\n", Status));
    return;
  }

  ZeroMem ((VOID *)(UINTN)New, EFI_PAGE_SIZE);
  CopyMem ((VOID *)(UINTN)New, Facs, Facs->Length);
  ((EFI_ACPI_2_0_FIRMWARE_ACPI_CONTROL_STRUCTURE *)(UINTN)New)->GlobalLock = 0;   // no owner across the copy
  Fadt->FirmwareCtrl = (UINT32)New;
  if (Fadt->Header.Length >= OFFSET_OF (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE, XDsdt)) {
    Fadt->XFirmwareCtrl = 0;
  }

  Fadt->Header.Checksum = 0;
  Fadt->Header.Checksum = CalculateCheckSum8 ((UINT8 *)Fadt, Fadt->Header.Length);
  DEBUG ((DEBUG_INFO, "Ps4Acpi: FACS moved %lx (not ACPI NVS) -> %lx (ACPI NVS), %u bytes\n", Old, New, Facs->Length));
}

STATIC
BOOLEAN
PatchAml (
  IN OUT UINT8   *Aml,
  IN     UINTN   Length,
  IN     UINT64  Placeholder,
  IN     UINT64  Value,
  IN     BOOLEAN Qword
  )
{
  UINTN  I;
  UINTN  W;

  W = Qword ? 8 : 4;
  for (I = 0; I + 1 + W <= Length; I++) {
    if ((Aml[I] == (Qword ? 0x0E : 0x0C)) && (CompareMem (&Aml[I + 1], &Placeholder, W) == 0)) {
      CopyMem (&Aml[I + 1], &Value, W);
      return TRUE;
    }
  }

  DEBUG ((DEBUG_ERROR, "Ps4Acpi: AML placeholder %lx not found\n", Placeholder));
  return FALSE;
}

/* Read-only check: every live memory BAR on bus 0 should fall inside one of the root-bridge windows. */
STATIC
VOID
CheckBars (
  IN UINT64  Ecam,
  IN UINT64  W[3][2]
  )
{
  UINTN   Dev;
  UINTN   Fn;
  UINTN   B;
  UINTN   Cfg;
  UINT32  Id;
  UINT32  Lo;
  UINT64  Base;
  UINTN   K;
  UINTN   Seen;
  UINTN   Outside;

  Seen    = 0;
  Outside = 0;
  for (Dev = 0; Dev < 32; Dev++) {
    for (Fn = 0; Fn < 8; Fn++) {
      Cfg = (UINTN)Ecam + (Dev << 15) + (Fn << 12);
      Id  = MmioRead32 (Cfg);
      if (((Id & 0xFFFF) == 0xFFFF) || ((Id & 0xFFFF) == 0)) {
        if (Fn == 0) {
          break;
        }

        continue;
      }

      if (((Id & 0xFFFF) == 0x104D) && (Dev != 0x14)) {         // Aeolia aliases on other device numbers
        break;
      }

      if ((MmioRead8 (Cfg + 0x0E) & 0x7F) != 0) {               // bridges: BARs 0-1 only; skipped here
        continue;
      }

      for (B = 0; B < 6; B++) {
        Lo = MmioRead32 (Cfg + 0x10 + B * 4);
        if ((Lo & 1) != 0) {
          continue;                                             // I/O BAR
        }

        Base = Lo & ~0xFULL;
        if (((Lo >> 1) & 3) == 2) {                             // 64-bit
          Base |= LShiftU64 (MmioRead32 (Cfg + 0x14 + B * 4), 32);
          B++;
        }

        if (Base == 0) {
          continue;
        }

        Seen++;
        for (K = 0; K < 3; K++) {
          if ((W[K][1] > W[K][0]) && (Base >= W[K][0]) && (Base <= W[K][1])) {
            break;
          }
        }

        if (K == 3) {
          Outside++;
          DEBUG ((DEBUG_WARN, "Ps4Acpi: BAR 00:%02x.%x[%u] = %lx is OUTSIDE the root-bridge windows\n", Dev, Fn, B, Base));
        }
      }

      if ((Fn == 0) && ((MmioRead8 (Cfg + 0x0E) & 0x80) == 0)) {
        break;
      }
    }
  }

  DEBUG ((DEBUG_INFO, "Ps4Acpi: %u memory BARs checked, %u outside the windows\n", Seen, Outside));
}

STATIC
VOID
InstallDsdt (
  IN EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE  *Fadt
  )
{
  EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER  *Mcfg;
  EFI_ACPI_MEMORY_MAPPED_ENHANCED_CONFIGURATION_SPACE_BASE_ADDRESS_ALLOCATION_STRUCTURE  *Alloc;
  UINT64                *W;
  UINT64                Win[3][2];
  UINT64                Ecam;
  UINT64                EcamLen;
  UINT64                BusMax;
  UINT64                LowTop;
  UINT64                HighTop;
  UINT64                PhysTop;
  UINT32                Eax;
  EFI_PHYSICAL_ADDRESS  New;
  UINT8                 *Aml;
  BOOLEAN               Ok;
  PS4_RTC_MAILBOX       *Mbox;
  UINT64                RtcBase;
  UINT64                RtcMbox;

  Ecam   = 0xF8000000;                                          // Sony MCFG (9.00 dump) as the fallback
  BusMax = 0x3F;
  Mcfg   = (VOID *)FindTable (EFI_ACPI_6_4_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE);
  if ((Mcfg != NULL) && (Mcfg->Header.Length >= sizeof (*Mcfg) + sizeof (*Alloc))) {
    Alloc  = (VOID *)(Mcfg + 1);
    Ecam   = Alloc->BaseAddress + LShiftU64 (Alloc->StartBusNumber, 20);
    BusMax = Alloc->EndBusNumber;
  }

  EcamLen = LShiftU64 (BusMax + 1, 20);
  LowTop  = ALIGN_VALUE (MapTop (0, BASE_4GB), SIZE_256MB);
  HighTop = ALIGN_VALUE (MapTop (BASE_4GB, MAX_UINT64), SIZE_1GB);
  if (HighTop < BASE_4GB) {
    HighTop = BASE_4GB;
  }

  AsmCpuid (0x80000000, &Eax, NULL, NULL, NULL);
  PhysTop = BIT36;
  if (Eax >= 0x80000008) {
    AsmCpuid (0x80000008, &Eax, NULL, NULL, NULL);
    PhysTop = LShiftU64 (1, Eax & 0xFF);
  }

  if (PhysTop > HT_HOLE_BASE) {
    PhysTop = HT_HOLE_BASE;
  }

  if ((Ecam < LowTop) || (Ecam + EcamLen > MMIO_TOP_32 + 1)) {
    DEBUG ((DEBUG_ERROR, "Ps4Acpi: ECAM %lx+%lx not between low RAM top %lx and %lx; DSDT not replaced\n",
            Ecam, EcamLen, LowTop, MMIO_TOP_32));
    return;
  }

  Win[0][0] = LowTop;           Win[0][1] = Ecam - 1;
  Win[1][0] = Ecam + EcamLen;   Win[1][1] = MMIO_TOP_32;
  Win[2][0] = HighTop;          Win[2][1] = PhysTop - 1;
  if ((Win[0][1] < Win[0][0]) || (Win[1][1] < Win[1][0])) {
    DEBUG ((DEBUG_ERROR, "Ps4Acpi: empty 32-bit window (ECAM at an edge); DSDT not replaced\n"));
    return;
  }

  New = BASE_4GB - 1;
  if (EFI_ERROR (gBS->AllocatePages (AllocateMaxAddress, EfiACPIReclaimMemory, EFI_SIZE_TO_PAGES (sizeof (mPs4DsdtAml)), &New))) {
    DEBUG ((DEBUG_ERROR, "Ps4Acpi: no memory for the DSDT\n"));
    return;
  }

  Aml = (UINT8 *)(UINTN)New;
  CopyMem (Aml, mPs4DsdtAml, sizeof (mPs4DsdtAml));
  W  = &Win[0][0];

  /* RTC: the Time and Alarm Device reports absent (_STA 0) unless Ps4RtcDxe found the Aeolia counter */
  RtcBase = 0;
  RtcMbox = 0;
  if (!EFI_ERROR (EfiGetSystemConfigurationTable (&gPs4RtcMailboxGuid, (VOID **)&Mbox)) && (Mbox != NULL) &&
      (Mbox->Signature == PS4_RTC_MAILBOX_SIGNATURE) && ((Mbox->Flags & PS4_RTC_FLAG_COUNTER) != 0))
  {
    RtcBase = Mbox->CounterBase;
    RtcMbox = (UINTN)Mbox;
  }

  Ok = PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0001, Ecam, FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0002, EcamLen, FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0003, BusMax, FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0004, W[0], FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0005, W[1], FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0006, W[2], FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A0007, W[3], FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A000A, (W[5] > W[4]) ? 1 : 0, FALSE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A5A5A5A5A0008ull, W[4], TRUE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A5A5A5A5A0009ull, W[5], TRUE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A5A5A5A5A000Bull, RtcBase, TRUE) &&
       PatchAml (Aml, sizeof (mPs4DsdtAml), 0x5A5A5A5A5A5A000Cull, RtcMbox, TRUE);
  if (!Ok) {
    gBS->FreePages (New, EFI_SIZE_TO_PAGES (sizeof (mPs4DsdtAml)));
    return;
  }

  ((EFI_ACPI_DESCRIPTION_HEADER *)Aml)->Checksum = 0;
  ((EFI_ACPI_DESCRIPTION_HEADER *)Aml)->Checksum = CalculateCheckSum8 (Aml, sizeof (mPs4DsdtAml));
  DEBUG ((DEBUG_INFO, "Ps4Acpi: DSDT replaced (%lx -> %lx): ECAM %lx buses 0-%lx, windows %lx-%lx %lx-%lx %lx-%lx\n",
          (Fadt->XDsdt != 0) ? Fadt->XDsdt : Fadt->Dsdt, New, Ecam, BusMax, W[0], W[1], W[2], W[3], W[4], W[5]));
  DEBUG ((DEBUG_INFO, "Ps4Acpi: ACPI TAD (ACPI000E) %a: counter %lx, mailbox %lx\n",
          (RtcMbox != 0) ? "present" : "absent (no RTC counter)", RtcBase, RtcMbox));
  Fadt->Dsdt = (UINT32)New;
  if (Fadt->Header.Length >= OFFSET_OF (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE, XPm1aEvtBlk)) {
    Fadt->XDsdt = New;
  }

  CheckBars (Ecam, Win);
}

/* Harness: make the MADT look like the PS4's (only type-0 local APIC entries, flags 0). */
STATIC
VOID
SonyMadt (
  IN UINT32  Flags
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Madt;
  UINT8                        *Src;
  UINT8                        *Dst;
  UINT8                        *End;
  UINTN                        Kept;
  UINTN                        Dropped;

  Madt = FindTable (EFI_ACPI_2_0_MULTIPLE_SAPIC_DESCRIPTION_TABLE_SIGNATURE);
  if (Madt == NULL) {
    return;
  }

  Src     = (UINT8 *)Madt + sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 8;
  Dst     = Src;
  End     = (UINT8 *)Madt + Madt->Length;
  Kept    = 0;
  Dropped = 0;
  while ((Src + 2 <= End) && (Src[1] >= 2) && (Src + Src[1] <= End)) {
    if ((Src[0] == EFI_ACPI_2_0_PROCESSOR_LOCAL_APIC) ||
        ((Src[0] == EFI_ACPI_2_0_IO_APIC) && ((Flags & PS4_DIRECTBOOT_FLAG_KEEP_IOAPIC) != 0)))
    {
      CopyMem (Dst, Src, Src[1]);
      Dst += Src[1];
      Kept++;
    } else {
      Dropped++;
    }

    Src += Src[1];
  }

  if ((Flags & PS4_DIRECTBOOT_FLAG_KEEP_PCAT) == 0) {
    WriteUnaligned32 ((UINT32 *)((UINT8 *)Madt + sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 4), 0);   // PCAT_COMPAT = 0
  }

  Madt->Length   = (UINT32)(Dst - (UINT8 *)Madt);
  Madt->Checksum = 0;
  Madt->Checksum = CalculateCheckSum8 ((UINT8 *)Madt, Madt->Length);
  DEBUG ((DEBUG_INFO, "Ps4Acpi: harness MADT reduced: %u entries kept, %u dropped, flags %x\n", Kept, Dropped,
          ReadUnaligned32 ((UINT32 *)((UINT8 *)Madt + sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 4))));
}

/* Replace the XSDT/RSDT entry pointing at Old with New (checksums fixed). */
STATIC
VOID
RepointRootTables (
  IN UINT64  Old,
  IN UINT64  New
  )
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *T;
  UINTN                                         N;
  UINTN                                         I;

  if (EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp)) || (Rsdp == NULL)) {
    return;
  }

  if (Rsdp->XsdtAddress != 0) {
    T = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
    N = (T->Length - sizeof (*T)) / sizeof (UINT64);
    for (I = 0; I < N; I++) {
      if (ReadUnaligned64 ((UINT64 *)(T + 1) + I) == Old) {
        WriteUnaligned64 ((UINT64 *)(T + 1) + I, New);
      }
    }

    T->Checksum = 0;
    T->Checksum = CalculateCheckSum8 ((UINT8 *)T, T->Length);
  }

  if (Rsdp->RsdtAddress != 0) {
    T = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->RsdtAddress;
    N = (T->Length - sizeof (*T)) / sizeof (UINT32);
    for (I = 0; I < N; I++) {
      if (ReadUnaligned32 ((UINT32 *)(T + 1) + I) == (UINT32)Old) {
        WriteUnaligned32 ((UINT32 *)(T + 1) + I, (UINT32)New);
      }
    }

    T->Checksum = 0;
    T->Checksum = CalculateCheckSum8 ((UINT8 *)T, T->Length);
  }
}

STATIC
VOID
AddRamIoApic (
  VOID
  )
{
  EFI_ACPI_DESCRIPTION_HEADER  *Madt;
  EFI_ACPI_DESCRIPTION_HEADER  *NewMadt;
  UINT8                        *E;
  UINT8                        *End;
  EFI_PHYSICAL_ADDRESS         Page;
  EFI_PHYSICAL_ADDRESS         Copy;
  UINT8                        Entry[12];

  Madt = FindTable (EFI_ACPI_2_0_MULTIPLE_SAPIC_DESCRIPTION_TABLE_SIGNATURE);
  if (Madt == NULL) {
    return;
  }

  E   = (UINT8 *)Madt + sizeof (EFI_ACPI_DESCRIPTION_HEADER) + 8;
  End = (UINT8 *)Madt + Madt->Length;
  for ( ; (E + 2 <= End) && (E[1] >= 2); E += E[1]) {
    if (E[0] == EFI_ACPI_2_0_IO_APIC) {
      return;                                                  // a real IOAPIC is listed
    }
  }

  Page = BASE_4GB - 1;
  Copy = BASE_4GB - 1;
  if (EFI_ERROR (gBS->AllocatePages (AllocateMaxAddress, EfiReservedMemoryType, 1, &Page)) ||
      EFI_ERROR (gBS->AllocatePages (AllocateMaxAddress, EfiACPIReclaimMemory, EFI_SIZE_TO_PAGES (Madt->Length + sizeof (Entry)), &Copy)))
  {
    DEBUG ((DEBUG_ERROR, "Ps4Acpi: no memory for the RAM IOAPIC\n"));
    return;
  }

  ZeroMem ((VOID *)(UINTN)Page, EFI_PAGE_SIZE);
  *(UINT32 *)(UINTN)(Page + 0x10) = 0x00170011;                 // IOWIN: version 0x11, max redirection entry 0x17

  ZeroMem (Entry, sizeof (Entry));
  Entry[0] = EFI_ACPI_2_0_IO_APIC;                              // type 1, length 12
  Entry[1] = sizeof (Entry);
  Entry[2] = 0;                                                 // = ID field (bits 24-27) the page returns for register 0
  WriteUnaligned32 ((UINT32 *)&Entry[4], (UINT32)Page);         // address
  WriteUnaligned32 ((UINT32 *)&Entry[8], 0);                    // GSI base

  NewMadt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Copy;
  CopyMem (NewMadt, Madt, Madt->Length);
  CopyMem ((UINT8 *)NewMadt + Madt->Length, Entry, sizeof (Entry));
  NewMadt->Length  += sizeof (Entry);
  NewMadt->Checksum = 0;
  NewMadt->Checksum = CalculateCheckSum8 ((UINT8 *)NewMadt, NewMadt->Length);
  RepointRootTables ((UINTN)Madt, Copy);
  DEBUG ((DEBUG_INFO, "Ps4Acpi: MADT had no IOAPIC: RAM IOAPIC ID 0 at %lx (IOWIN 0x00170011), MADT %p -> %lx\n",
          Page, Madt, Copy));
}

/*
  Tell the OS there is no CMOS RTC. Returns the FADT now in the root tables (a revision-6 copy when the old one
  was shorter, as Sony's 244-byte revision-4 table is).
*/
STATIC
EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE *
NoCmosRtcFadt (
  IN EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE  *Fadt
  )
{
  EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE  *F;
  EFI_PHYSICAL_ADDRESS                       New;
  UINT8                                      OldRevision;
  UINT32                                     OldLength;

  OldRevision = Fadt->Header.Revision;
  OldLength   = Fadt->Header.Length;
  F           = Fadt;
  if ((OldLength < sizeof (*F)) || (OldRevision < EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_REVISION)) {
    New = BASE_4GB - 1;                                         // RSDT entries are 32-bit
    if (EFI_ERROR (gBS->AllocatePages (AllocateMaxAddress, EfiACPIReclaimMemory, EFI_SIZE_TO_PAGES (sizeof (*F)), &New))) {
      DEBUG ((DEBUG_ERROR, "Ps4Acpi: no memory for the revision-6 FADT; CMOS RTC still advertised\n"));
      return Fadt;
    }

    F = (EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE *)(UINTN)New;
    ZeroMem (F, sizeof (*F));
    CopyMem (F, Fadt, MIN (OldLength, sizeof (*F)));
    if (OldLength < sizeof (*F)) {
      F->Header.Length = sizeof (*F);                           // new fields (sleep regs, hypervisor id) stay 0
    }

    F->Header.Revision = EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_REVISION;
    F->MinorVersion    = EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_MINOR_REVISION;
  }

  F->IaPcBootArch |= EFI_ACPI_6_0_CMOS_RTC_NOT_PRESENT;
  F->DayAlrm       = 0;                                         // no CMOS RAM indexes
  F->MonAlrm       = 0;
  F->Century       = 0;
  F->Flags        |= EFI_ACPI_6_0_FIX_RTC;                      // no RTC wake status in the PM1 fixed registers
  F->Flags        &= ~(UINT32)EFI_ACPI_6_0_RTC_S4;
  F->Header.Checksum = 0;
  F->Header.Checksum = CalculateCheckSum8 ((UINT8 *)F, F->Header.Length);
  if (F != Fadt) {
    RepointRootTables ((UINTN)Fadt, (UINTN)F);
  }

  DEBUG ((DEBUG_INFO, "Ps4Acpi: FADT rev %u (%u B) -> rev %u (%u B) at %p: IAPC_BOOT_ARCH %x (CMOS RTC not present), flags %x\n",
          OldRevision, OldLength, F->Header.Revision, F->Header.Length, F, F->IaPcBootArch, F->Flags));
  return F;
}

EFI_STATUS
EFIAPI
Ps4AcpiEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE  *Fadt;
  VOID                                       *Hob;
  UINT32                                     Flags;

  FixFacs ();
  Fadt = FindFadt ();
  if (Fadt == NULL) {
    return EFI_SUCCESS;
  }

  Hob   = GetFirstGuidHob (&gPs4DirectbootInfoGuid);
  Flags = (Hob != NULL) ? ((PS4_DIRECTBOOT_INFO *)GET_GUID_HOB_DATA (Hob))->Flags : 0;
  if ((CompareMem (Fadt->Header.OemId, "SIE", 3) == 0) || ((Flags & PS4_DIRECTBOOT_FLAG_FORCE_PS4_DSDT) != 0)) {
    InstallDsdt (Fadt);
    Fadt->Header.Checksum = 0;
    Fadt->Header.Checksum = CalculateCheckSum8 ((UINT8 *)Fadt, Fadt->Header.Length);
    Fadt = NoCmosRtcFadt (Fadt);
  } else {
    DEBUG ((DEBUG_INFO, "Ps4Acpi: tables are not Sony's (OEM %.6a); DSDT kept\n", Fadt->Header.OemId));
  }

  if ((Flags & PS4_DIRECTBOOT_FLAG_SONY_MADT) != 0) {
    SonyMadt (Flags);
  }

  AddRamIoApic ();

  return EFI_SUCCESS;
}
