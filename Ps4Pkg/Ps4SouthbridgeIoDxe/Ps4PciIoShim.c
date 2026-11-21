/** @file
  Ps4PciIoShim - a minimal EFI_PCI_IO_PROTOCOL for one live southbridge function (the GbE), for
  drivers that need real config space (Ps4YukonDxe) where NonDiscoverablePciDeviceDxe's emulated
  config space is not enough.

  Rule kept: the command register and BARs of a device OrbisOS left running are never written.
  Config writes are allowed only to the status register (16-bit, write-1-to-clear error bits) and to
  device-specific registers at 0x40 and above. Attributes() is a no-op (decode and bus master are
  already on, as OrbisOS left them). DMA is identity-mapped and limited to the first 2 GiB (the
  southbridge functions are 31-bit DMA masters); everything EDK II owns on the PS4 is below 2 GiB.
**/

#include <PiDxe.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Pci.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PciIo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "Ps4PciIoShim.h"

#define SHIM_SIGNATURE  SIGNATURE_32 ('P', '4', 'P', 'S')
#define DMA_LIMIT       0x7FFFFFFFULL

#pragma pack(1)
typedef struct {
  ACPI_HID_DEVICE_PATH        Root;
  PCI_DEVICE_PATH             Pci;
  EFI_DEVICE_PATH_PROTOCOL    End;
} SHIM_DEVICE_PATH;
#pragma pack()

typedef struct {
  UINT32                 Signature;
  EFI_PCI_IO_PROTOCOL    PciIo;
  SHIM_DEVICE_PATH       DevicePath;
  UINTN                  Cfg;                 // ECAM address of this function
  UINT64                 Bar[6];              // memory BAR bases (0 = not served)
  UINT64                 BarSize[6];
  UINT8                  Device;
  UINT8                  Function;
  UINT64                 Attributes;
} SHIM;

#define SHIM_FROM_PCIIO(a)  CR (a, SHIM, PciIo, SHIM_SIGNATURE)

typedef struct {                       // one bounce mapping (buffer above the 2 GiB DMA limit)
  VOID                             *Host;
  VOID                             *Bounce;
  UINTN                            Bytes;
  EFI_PCI_IO_PROTOCOL_OPERATION    Operation;
} SHIM_BOUNCE;

STATIC
UINTN
WidthBytes (
  IN EFI_PCI_IO_PROTOCOL_WIDTH  Width
  )
{
  return (UINTN)1 << (Width & 0x03);
}

STATIC
EFI_STATUS
EFIAPI
ShimPciRead (
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT32                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  )
{
  SHIM   *S;
  UINTN  Size;
  UINTN  I;

  S    = SHIM_FROM_PCIIO (This);
  Size = WidthBytes (Width);
  if ((Buffer == NULL) || (Width >= EfiPciIoWidthMaximum) || (Offset + Size * Count > 0x1000)) {
    return EFI_INVALID_PARAMETER;
  }

  for (I = 0; I < Count; I++, Offset += (UINT32)Size) {
    switch (Size) {
      case 1: ((UINT8 *)Buffer)[I]  = MmioRead8 (S->Cfg + Offset);  break;
      case 2: ((UINT16 *)Buffer)[I] = MmioRead16 (S->Cfg + Offset); break;
      case 4: ((UINT32 *)Buffer)[I] = MmioRead32 (S->Cfg + Offset); break;
      default: return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimPciWrite (
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT32                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  )
{
  SHIM   *S;
  UINTN  Size;
  UINTN  I;

  S    = SHIM_FROM_PCIIO (This);
  Size = WidthBytes (Width);
  if ((Buffer == NULL) || (Width >= EfiPciIoWidthMaximum) || (Offset + Size * Count > 0x1000)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Header: only the 16-bit status register (RW1C error bits). Command, BARs, the rest: refused.
  //
  if ((Offset < 0x40) && !((Offset == PCI_PRIMARY_STATUS_OFFSET) && (Size == 2) && (Count == 1))) {
    DEBUG ((DEBUG_WARN, "Ps4PciIoShim: 00:%02x.%x config write at 0x%x refused (live device)\n", S->Device, S->Function, Offset));
    return EFI_ACCESS_DENIED;
  }

  for (I = 0; I < Count; I++, Offset += (UINT32)Size) {
    switch (Size) {
      case 1: MmioWrite8 (S->Cfg + Offset, ((UINT8 *)Buffer)[I]);   break;
      case 2: MmioWrite16 (S->Cfg + Offset, ((UINT16 *)Buffer)[I]); break;
      case 4: MmioWrite32 (S->Cfg + Offset, ((UINT32 *)Buffer)[I]); break;
      default: return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimMemRead (
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT8                      BarIndex,
  IN     UINT64                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  )
{
  SHIM   *S;
  UINTN  Size;
  UINTN  I;

  S    = SHIM_FROM_PCIIO (This);
  Size = WidthBytes (Width);
  if ((BarIndex >= 6) || (S->BarSize[BarIndex] == 0) || (Buffer == NULL) || (Offset + Size * Count > S->BarSize[BarIndex])) {
    return EFI_UNSUPPORTED;
  }

  for (I = 0; I < Count; I++, Offset += Size) {
    switch (Size) {
      case 1: ((UINT8 *)Buffer)[I]  = MmioRead8 ((UINTN)(S->Bar[BarIndex] + Offset));  break;
      case 2: ((UINT16 *)Buffer)[I] = MmioRead16 ((UINTN)(S->Bar[BarIndex] + Offset)); break;
      case 4: ((UINT32 *)Buffer)[I] = MmioRead32 ((UINTN)(S->Bar[BarIndex] + Offset)); break;
      default: return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimMemWrite (
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT8                      BarIndex,
  IN     UINT64                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  )
{
  SHIM   *S;
  UINTN  Size;
  UINTN  I;

  S    = SHIM_FROM_PCIIO (This);
  Size = WidthBytes (Width);
  if ((BarIndex >= 6) || (S->BarSize[BarIndex] == 0) || (Buffer == NULL) || (Offset + Size * Count > S->BarSize[BarIndex])) {
    return EFI_UNSUPPORTED;
  }

  for (I = 0; I < Count; I++, Offset += Size) {
    switch (Size) {
      case 1: MmioWrite8 ((UINTN)(S->Bar[BarIndex] + Offset), ((UINT8 *)Buffer)[I]);   break;
      case 2: MmioWrite16 ((UINTN)(S->Bar[BarIndex] + Offset), ((UINT16 *)Buffer)[I]); break;
      case 4: MmioWrite32 ((UINTN)(S->Bar[BarIndex] + Offset), ((UINT32 *)Buffer)[I]); break;
      default: return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimPollMem (
  IN  EFI_PCI_IO_PROTOCOL        *This,
  IN  EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN  UINT8                      BarIndex,
  IN  UINT64                     Offset,
  IN  UINT64                     Mask,
  IN  UINT64                     Value,
  IN  UINT64                     Delay,
  OUT UINT64                     *Result
  )
{
  EFI_STATUS  Status;
  UINT64      Waited;

  if (Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  for (Waited = 0; ; Waited += 10) {                     // Delay is in 100 ns units
    *Result = 0;
    Status  = ShimMemRead (This, Width, BarIndex, Offset, 1, Result);
    if (EFI_ERROR (Status) || ((*Result & Mask) == Value)) {
      return Status;
    }

    if (Waited >= Delay) {
      return EFI_TIMEOUT;
    }

    gBS->Stall (1);
  }
}

STATIC
EFI_STATUS
EFIAPI
ShimPollIo (
  IN  EFI_PCI_IO_PROTOCOL        *This,
  IN  EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN  UINT8                      BarIndex,
  IN  UINT64                     Offset,
  IN  UINT64                     Mask,
  IN  UINT64                     Value,
  IN  UINT64                     Delay,
  OUT UINT64                     *Result
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
ShimIo (
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT8                      BarIndex,
  IN     UINT64                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  )
{
  return EFI_UNSUPPORTED;                               // no I/O BARs on these functions
}

STATIC
EFI_STATUS
EFIAPI
ShimCopyMem (
  IN EFI_PCI_IO_PROTOCOL        *This,
  IN EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN UINT8                      DestBarIndex,
  IN UINT64                     DestOffset,
  IN UINT8                      SrcBarIndex,
  IN UINT64                     SrcOffset,
  IN UINTN                      Count
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
ShimMap (
  IN     EFI_PCI_IO_PROTOCOL            *This,
  IN     EFI_PCI_IO_PROTOCOL_OPERATION  Operation,
  IN     VOID                           *HostAddress,
  IN OUT UINTN                          *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS           *DeviceAddress,
  OUT    VOID                           **Mapping
  )
{
  SHIM_BOUNCE           *B;
  EFI_PHYSICAL_ADDRESS  Low;
  EFI_STATUS            Status;

  if ((HostAddress == NULL) || (NumberOfBytes == NULL) || (DeviceAddress == NULL) || (Mapping == NULL) ||
      (*NumberOfBytes == 0) || ((UINTN)Operation >= EfiPciIoOperationMaximum))
  {
    return EFI_INVALID_PARAMETER;
  }

  if ((UINT64)(UINTN)HostAddress + *NumberOfBytes - 1 <= DMA_LIMIT) {
    *DeviceAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress;   // IOMMU off: device address = physical
    *Mapping       = NULL;                                       // direct
    return EFI_SUCCESS;
  }

  //
  // RAM above 4 GiB exists now. The southbridge functions are 31-bit DMA masters, so a
  // buffer above 2 GiB goes through a bounce buffer below 2 GiB. A common buffer must come from
  // AllocateBuffer (always below 2 GiB): both sides see it live, a copy cannot serve that.
  //
  if (Operation == EfiPciIoOperationBusMasterCommonBuffer) {
    DEBUG ((DEBUG_ERROR, "Ps4PciIoShim: common buffer %p above 2 GiB refused (use AllocateBuffer)\n", HostAddress));
    return EFI_UNSUPPORTED;
  }

  B = AllocateZeroPool (sizeof (*B));
  if (B == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Low    = DMA_LIMIT;
  Status = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES (*NumberOfBytes), &Low);
  if (EFI_ERROR (Status)) {
    FreePool (B);
    return Status;
  }

  B->Host      = HostAddress;
  B->Bounce    = (VOID *)(UINTN)Low;
  B->Bytes     = *NumberOfBytes;
  B->Operation = Operation;
  if (Operation == EfiPciIoOperationBusMasterRead) {
    CopyMem (B->Bounce, HostAddress, B->Bytes);                  // device reads: data in before DMA
  }

  *DeviceAddress = Low;
  *Mapping       = B;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimUnmap (
  IN EFI_PCI_IO_PROTOCOL  *This,
  IN VOID                 *Mapping
  )
{
  SHIM_BOUNCE  *B;

  B = Mapping;
  if (B == NULL) {
    return EFI_SUCCESS;                                          // direct mapping
  }

  if (B->Operation == EfiPciIoOperationBusMasterWrite) {
    CopyMem (B->Host, B->Bounce, B->Bytes);                      // device wrote: data out after DMA
  }

  gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)B->Bounce, EFI_SIZE_TO_PAGES (B->Bytes));
  FreePool (B);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimAllocateBuffer (
  IN  EFI_PCI_IO_PROTOCOL  *This,
  IN  EFI_ALLOCATE_TYPE    Type,
  IN  EFI_MEMORY_TYPE      MemoryType,
  IN  UINTN                Pages,
  OUT VOID                 **HostAddress,
  IN  UINT64               Attributes
  )
{
  EFI_PHYSICAL_ADDRESS  Address;
  EFI_STATUS            Status;

  if (HostAddress == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Address = DMA_LIMIT;
  Status  = gBS->AllocatePages (AllocateMaxAddress, MemoryType, Pages, &Address);
  if (!EFI_ERROR (Status)) {
    *HostAddress = (VOID *)(UINTN)Address;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
ShimFreeBuffer (
  IN EFI_PCI_IO_PROTOCOL  *This,
  IN UINTN                Pages,
  IN VOID                 *HostAddress
  )
{
  return gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress, Pages);
}

STATIC
EFI_STATUS
EFIAPI
ShimFlush (
  IN EFI_PCI_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimGetLocation (
  IN  EFI_PCI_IO_PROTOCOL  *This,
  OUT UINTN                *SegmentNumber,
  OUT UINTN                *BusNumber,
  OUT UINTN                *DeviceNumber,
  OUT UINTN                *FunctionNumber
  )
{
  SHIM  *S;

  if ((SegmentNumber == NULL) || (BusNumber == NULL) || (DeviceNumber == NULL) || (FunctionNumber == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  S               = SHIM_FROM_PCIIO (This);
  *SegmentNumber  = 0;
  *BusNumber      = 0;
  *DeviceNumber   = S->Device;
  *FunctionNumber = S->Function;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimAttributes (
  IN  EFI_PCI_IO_PROTOCOL                      *This,
  IN  EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION  Operation,
  IN  UINT64                                   Attributes,
  OUT UINT64                                   *Result OPTIONAL
  )
{
  SHIM          *S;
  CONST UINT64  Supported = EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE;

  S = SHIM_FROM_PCIIO (This);
  switch (Operation) {
    case EfiPciIoAttributeOperationGet:
    case EfiPciIoAttributeOperationSupported:
      if (Result == NULL) {
        return EFI_INVALID_PARAMETER;
      }

      *Result = (Operation == EfiPciIoAttributeOperationGet) ? S->Attributes : Supported;
      return EFI_SUCCESS;
    case EfiPciIoAttributeOperationSet:
      S->Attributes = Attributes & Supported;          // bookkeeping only: hardware already enabled
      return EFI_SUCCESS;
    case EfiPciIoAttributeOperationEnable:
      S->Attributes |= Attributes & Supported;
      return EFI_SUCCESS;
    case EfiPciIoAttributeOperationDisable:
      S->Attributes &= ~Attributes;                    // never turns the live device off
      return EFI_SUCCESS;
    default:
      return EFI_INVALID_PARAMETER;
  }
}

STATIC
EFI_STATUS
EFIAPI
ShimGetBarAttributes (
  IN  EFI_PCI_IO_PROTOCOL  *This,
  IN  UINT8                BarIndex,
  OUT UINT64               *Supports OPTIONAL,
  OUT VOID                 **Resources OPTIONAL
  )
{
  SHIM                               *S;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Desc;
  EFI_ACPI_END_TAG_DESCRIPTOR        *End;

  S = SHIM_FROM_PCIIO (This);
  if ((BarIndex >= 6) || (S->BarSize[BarIndex] == 0)) {
    return EFI_UNSUPPORTED;
  }

  if (Supports != NULL) {
    *Supports = 0;
  }

  if (Resources != NULL) {
    Desc = AllocateZeroPool (sizeof (*Desc) + sizeof (*End));
    if (Desc == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Desc->Desc                 = ACPI_ADDRESS_SPACE_DESCRIPTOR;
    Desc->Len                  = (UINT16)(sizeof (*Desc) - 3);
    Desc->ResType              = ACPI_ADDRESS_SPACE_TYPE_MEM;
    Desc->AddrSpaceGranularity = (S->Bar[BarIndex] >= BASE_4GB) ? 64 : 32;
    Desc->AddrRangeMin         = S->Bar[BarIndex];
    Desc->AddrRangeMax         = S->Bar[BarIndex] + S->BarSize[BarIndex] - 1;
    Desc->AddrLen              = S->BarSize[BarIndex];
    End                        = (EFI_ACPI_END_TAG_DESCRIPTOR *)(Desc + 1);
    End->Desc                  = ACPI_END_TAG_DESCRIPTOR;
    *Resources                 = Desc;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ShimSetBarAttributes (
  IN     EFI_PCI_IO_PROTOCOL  *This,
  IN     UINT64               Attributes,
  IN     UINT8                BarIndex,
  IN OUT UINT64               *Offset,
  IN OUT UINT64               *Length
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
Ps4InstallPciIoShim (
  IN  UINTN        Ecam,
  IN  UINT8        Device,
  IN  UINT8        Function,
  IN  CONST UINT64 *Bars,                  // 6 entries: memory BAR bases
  IN  CONST UINT64 *BarSizes,              // 6 entries: 0 = BAR not served
  OUT EFI_HANDLE   *Handle
  )
{
  UINTN  B;
  SHIM  *S;

  S = AllocateZeroPool (sizeof (*S));
  if (S == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  S->Signature  = SHIM_SIGNATURE;
  S->Cfg        = Ecam + ((UINTN)Device << 15) + ((UINTN)Function << 12);
  for (B = 0; B < 6; B++) {
    S->Bar[B]     = Bars[B];
    S->BarSize[B] = BarSizes[B];
  }

  S->Device     = Device;
  S->Function   = Function;
  S->Attributes = EFI_PCI_DEVICE_ENABLE;              // as OrbisOS left it (checked by the caller)

  S->PciIo.PollMem             = ShimPollMem;
  S->PciIo.PollIo              = ShimPollIo;
  S->PciIo.Mem.Read            = ShimMemRead;
  S->PciIo.Mem.Write           = ShimMemWrite;
  S->PciIo.Io.Read             = ShimIo;
  S->PciIo.Io.Write            = ShimIo;
  S->PciIo.Pci.Read            = ShimPciRead;
  S->PciIo.Pci.Write           = ShimPciWrite;
  S->PciIo.CopyMem             = ShimCopyMem;
  S->PciIo.Map                 = ShimMap;
  S->PciIo.Unmap               = ShimUnmap;
  S->PciIo.AllocateBuffer      = ShimAllocateBuffer;
  S->PciIo.FreeBuffer          = ShimFreeBuffer;
  S->PciIo.Flush               = ShimFlush;
  S->PciIo.GetLocation         = ShimGetLocation;
  S->PciIo.Attributes          = ShimAttributes;
  S->PciIo.GetBarAttributes    = ShimGetBarAttributes;
  S->PciIo.SetBarAttributes    = ShimSetBarAttributes;
  S->PciIo.RomSize             = 0;
  S->PciIo.RomImage            = NULL;

  S->DevicePath.Root.Header.Type    = ACPI_DEVICE_PATH;
  S->DevicePath.Root.Header.SubType = ACPI_DP;
  SetDevicePathNodeLength (&S->DevicePath.Root.Header, sizeof (S->DevicePath.Root));
  S->DevicePath.Root.HID            = EISA_PNP_ID (0x0A03);
  S->DevicePath.Root.UID            = 0;
  S->DevicePath.Pci.Header.Type     = HARDWARE_DEVICE_PATH;
  S->DevicePath.Pci.Header.SubType  = HW_PCI_DP;
  SetDevicePathNodeLength (&S->DevicePath.Pci.Header, sizeof (S->DevicePath.Pci));
  S->DevicePath.Pci.Device          = Device;
  S->DevicePath.Pci.Function        = Function;
  SetDevicePathEndNode (&S->DevicePath.End);

  *Handle = NULL;
  return gBS->InstallMultipleProtocolInterfaces (
                Handle,
                &gEfiPciIoProtocolGuid,
                &S->PciIo,
                &gEfiDevicePathProtocolGuid,
                &S->DevicePath,
                NULL
                );
}
