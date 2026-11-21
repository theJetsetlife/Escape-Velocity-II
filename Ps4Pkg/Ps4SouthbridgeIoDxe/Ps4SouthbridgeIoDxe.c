/** @file
  Ps4SouthbridgeIoDxe - give EDK II's USB/SATA/SD drivers a PciIo for the PS4 southbridge controllers
  WITHOUT PCI enumeration.

  PciBusDxe is not in this firmware: it sizes BARs and rewrites command registers, and doing that to
  devices OrbisOS left running kills the console. Here, PCI configuration space is only ever READ. Each
  controller is registered as a non-discoverable MMIO device at the BAR address OrbisOS assigned;
  upstream NonDiscoverablePciDeviceDxe then produces a PciIo with emulated config space and attributes
  (no config writes), and the stock drivers (XhciDxe, SataController/AtaAtapiPassThru, SdMmcPciHcDxe)
  bind to it on EfiBootManagerConnectAll().

  Southbridge facts (PS4 Linux: xhci-aeolia.c, ahci.c, sdhci-pci-core.c, pci_ids.h):
  - all functions at bus 0 device 0x14; Aeolia also answers on every other device number, so Sony
    functions are only taken from device 0x14.
  - xHCI func 7 (class 0x088007, not standard): 3 controllers, each a whole BAR (BAR0/BAR2/BAR4);
    Belize/Baikal use BAR0/BAR4 only (BAR2 holds AHCI there).
  - AHCI func 2: ABAR at BAR5 (Aeolia/Belize). Baikal's AHCI needs a PHY init and is not registered.
  - SDHCI func 3: BAR0 (the slot holds the SDIO Wi-Fi/BT card).
  - all are 31-bit DMA devices; every RAM range EDK II owns on the PS4 is below 2 GiB.
  Other vendors' functions are matched by class code (used by the QEMU harness).
**/

#include <PiDxe.h>
#include <Guid/AcpiBoardInfoGuid.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include "Ps4PciIoShim.h"

#define SONY_VENDOR  0x104D
#define SB_DEVICE    0x14

#define PCI_CMD_MEM  BIT1
#define PCI_CMD_BME  BIT2

typedef struct {
  UINT16                          DeviceId;
  UINT8                           Function;
  NON_DISCOVERABLE_DEVICE_TYPE    Type;
  UINT8                           NumBars;
  UINT8                           Bars[3];
  UINT64                          Size;
  CONST CHAR8                     *Name;
} SB_FUNCTION;

STATIC CONST SB_FUNCTION  mSony[] = {
  { 0x90a4, 7, NonDiscoverableDeviceTypeXhci,  3, { 0, 2, 4 }, SIZE_2MB, "Aeolia xHCI"  },
  { 0x909f, 2, NonDiscoverableDeviceTypeAhci,  1, { 5 },       SIZE_4KB, "Aeolia AHCI"  },
  { 0x90a0, 3, NonDiscoverableDeviceTypeSdhci, 1, { 0 },       SIZE_4KB, "Aeolia SDHCI" },
  { 0x90cf, 7, NonDiscoverableDeviceTypeXhci,  2, { 0, 4 },    SIZE_2MB, "Belize xHCI"  },
  { 0x90ca, 2, NonDiscoverableDeviceTypeAhci,  1, { 5 },       SIZE_4KB, "Belize AHCI"  },
  { 0x90cb, 3, NonDiscoverableDeviceTypeSdhci, 1, { 0 },       SIZE_4KB, "Belize SDHCI" },
  { 0x90de, 7, NonDiscoverableDeviceTypeXhci,  2, { 0, 4 },    SIZE_2MB, "Baikal xHCI"  },
  { 0x90da, 3, NonDiscoverableDeviceTypeSdhci, 1, { 0 },       SIZE_4KB, "Baikal SDHCI" },
};

/* GbE (Marvell Yukon-2 Optima Prime, PS4 Linux sky2): real config space is needed by Ps4YukonDxe,
 * so it gets Ps4PciIoShim (write-filtered) instead of an emulated non-discoverable PciIo. */
STATIC CONST struct { UINT16 DeviceId; UINT8 Function; UINT64 Size; CONST CHAR8 *Name; } mSonyGbe[] = {
  { 0x909e, 1, SIZE_16KB, "Aeolia GbE" },
  { 0x90c9, 1, SIZE_16KB, "Belize GbE" },
};

typedef struct {
  UINT32                          ClassCode;          // base << 16 | sub << 8 | prog-if
  NON_DISCOVERABLE_DEVICE_TYPE    Type;
  UINT8                           Bar;
  UINT64                          Size;
  CONST CHAR8                     *Name;
} GENERIC_CLASS;

STATIC CONST GENERIC_CLASS  mGeneric[] = {
  { 0x0C0330, NonDiscoverableDeviceTypeXhci,  0, SIZE_64KB, "xHCI"  },
  { 0x010601, NonDiscoverableDeviceTypeAhci,  5, SIZE_4KB,  "AHCI"  },
  { 0x080501, NonDiscoverableDeviceTypeSdhci, 0, SIZE_4KB,  "SDHCI" },
};

STATIC UINTN  mEcam;

STATIC
UINT32
CfgRead32 (
  IN UINTN   Device,
  IN UINTN   Function,
  IN UINTN   Offset
  )
{
  return MmioRead32 (mEcam + (Device << 15) + (Function << 12) + Offset);   // bus 0; read only
}

/* Memory BAR address (64-bit aware). 0 = I/O BAR, unassigned, or past the BAR table. */
STATIC
UINT64
MemBar (
  IN UINTN  Device,
  IN UINTN  Function,
  IN UINTN  Bar
  )
{
  UINT32  Lo;
  UINT32  Hi;

  if (Bar > 5) {
    return 0;
  }

  Lo = CfgRead32 (Device, Function, 0x10 + Bar * 4);
  if ((Lo & BIT0) != 0) {
    return 0;                                         // I/O space BAR
  }

  Hi = 0;
  if (((Lo >> 1) & 3) == 2) {                         // 64-bit memory BAR
    if (Bar == 5) {
      return 0;
    }

    Hi = CfgRead32 (Device, Function, 0x10 + (Bar + 1) * 4);
  }

  return LShiftU64 (Hi, 32) | (Lo & ~(UINT32)0xF);
}

/* TRUE when the function decodes memory and may bus-master, as its OS left it (never changed here). */
STATIC
BOOLEAN
UsableAsLeft (
  IN UINTN        Device,
  IN UINTN        Function,
  IN CONST CHAR8  *Name
  )
{
  UINT32  Command;

  Command = CfgRead32 (Device, Function, 0x04) & 0xFFFF;
  if ((Command & (PCI_CMD_MEM | PCI_CMD_BME)) != (PCI_CMD_MEM | PCI_CMD_BME)) {
    DEBUG ((DEBUG_INFO, "Ps4SbIo: 00:%02x.%x %a skipped: command %04x (memory/bus-master off; not changed)\n", Device, Function, Name, Command));
    return FALSE;
  }

  return TRUE;
}

STATIC
UINTN
Register (
  IN NON_DISCOVERABLE_DEVICE_TYPE  Type,
  IN UINT64                        Base,
  IN UINT64                        Size,
  IN UINTN                         Device,
  IN UINTN                         Function,
  IN UINTN                         Bar,
  IN CONST CHAR8                   *Name
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  if ((Base == 0) || (Base >= BASE_4GB)) {
    DEBUG ((DEBUG_INFO, "Ps4SbIo: 00:%02x.%x %a BAR%u = 0x%lx unusable, skipped\n", Device, Function, Name, Bar, Base));
    return 0;
  }

  Handle = NULL;
  Status = RegisterNonDiscoverableMmioDevice (
             Type,
             NonDiscoverableDeviceDmaTypeCoherent,
             NULL,
             &Handle,
             1,
             (UINTN)Base,
             (UINTN)Size
             );
  DEBUG ((DEBUG_INFO, "Ps4SbIo: 00:%02x.%x %a BAR%u 0x%lx size 0x%lx: %r\n", Device, Function, Name, Bar, Base, Size, Status));
  return EFI_ERROR (Status) ? 0 : 1;
}

STATIC
UINTN
RegisterSony (
  VOID
  )
{
  UINTN   Index;
  UINTN   Bar;
  UINTN   Count;
  UINT32  Id;

  Count = 0;
  for (Index = 0; Index < ARRAY_SIZE (mSony); Index++) {
    Id = CfgRead32 (SB_DEVICE, mSony[Index].Function, 0x00);
    if (Id != ((UINT32)mSony[Index].DeviceId << 16 | SONY_VENDOR)) {
      continue;
    }

    if (!UsableAsLeft (SB_DEVICE, mSony[Index].Function, mSony[Index].Name)) {
      continue;
    }

    for (Bar = 0; Bar < mSony[Index].NumBars; Bar++) {
      Count += Register (
                 mSony[Index].Type,
                 MemBar (SB_DEVICE, mSony[Index].Function, mSony[Index].Bars[Bar]),
                 mSony[Index].Size,
                 SB_DEVICE,
                 mSony[Index].Function,
                 mSony[Index].Bars[Bar],
                 mSony[Index].Name
                 );
    }
  }

  for (Index = 0; Index < ARRAY_SIZE (mSonyGbe); Index++) {
    EFI_HANDLE  Handle;
    EFI_STATUS  Status;
    UINT64      Base;

    Id = CfgRead32 (SB_DEVICE, mSonyGbe[Index].Function, 0x00);
    if ((Id != ((UINT32)mSonyGbe[Index].DeviceId << 16 | SONY_VENDOR)) ||
        !UsableAsLeft (SB_DEVICE, mSonyGbe[Index].Function, mSonyGbe[Index].Name))
    {
      continue;
    }

    Base = MemBar (SB_DEVICE, mSonyGbe[Index].Function, 0);
    if ((Base == 0) || (Base >= BASE_4GB)) {
      DEBUG ((DEBUG_INFO, "Ps4SbIo: 00:%02x.%x %a BAR0 0x%lx unusable, skipped\n", SB_DEVICE, mSonyGbe[Index].Function, mSonyGbe[Index].Name, Base));
      continue;
    }

    {
      UINT64  Bars[6]  = { Base, 0, 0, 0, 0, 0 };
      UINT64  Sizes[6] = { mSonyGbe[Index].Size, 0, 0, 0, 0, 0 };
      Status = Ps4InstallPciIoShim (mEcam, SB_DEVICE, mSonyGbe[Index].Function, Bars, Sizes, &Handle);
    }
    DEBUG ((DEBUG_INFO, "Ps4SbIo: 00:%02x.%x %a BAR0 0x%lx size 0x%lx (PciIo shim): %r\n", SB_DEVICE, mSonyGbe[Index].Function, mSonyGbe[Index].Name, Base, mSonyGbe[Index].Size, Status));
    Count += EFI_ERROR (Status) ? 0 : 1;
  }

  return Count;
}

/* QEMU harness only: virtio NIC (vendor 0x1AF4, class 0x02) through the PciIo shim, so the network
 * stack and ps4log's LAN/DHCP/DNS/PXE tests run offline. No PS4 function has this vendor ID. BAR
 * extents come from the virtio PCI capability list (cfg_type/bar/offset/length): config is only read. */
STATIC
UINTN
RegisterVirtioNet (
  IN UINTN  Device,
  IN UINTN  Function
  )
{
  UINT64      Bars[6]  = { 0 };
  UINT64      Sizes[6] = { 0 };
  UINT32      Cap;
  UINT32      Hops;
  UINT32      Ext;
  UINT8       Bar;
  EFI_HANDLE  Handle;
  EFI_STATUS  Status;

  if ((CfgRead32 (Device, Function, 0x04) & (BIT20 << 0)) == 0) {      // status: capability list
    return 0;
  }

  Cap = CfgRead32 (Device, Function, 0x34) & 0xFC;
  for (Hops = 0; Cap >= 0x40 && Hops < 48; Hops++) {
    UINT32  Hdr;

    Hdr = CfgRead32 (Device, Function, Cap);
    if ((Hdr & 0xFF) == 0x09) {                                         // vendor-specific = virtio cap
      Bar = (UINT8)(CfgRead32 (Device, Function, Cap + 4) & 0xFF);
      Ext = CfgRead32 (Device, Function, Cap + 8) + CfgRead32 (Device, Function, Cap + 12);
      if ((Bar < 6) && (Ext > Sizes[Bar])) {
        Sizes[Bar] = Ext;
      }
    }

    Cap = (Hdr >> 8) & 0xFC;
  }

  for (Bar = 0; Bar < 6; Bar++) {
    if (Sizes[Bar] != 0) {
      Bars[Bar]  = MemBar (Device, Function, Bar);
      Sizes[Bar] = ALIGN_VALUE (Sizes[Bar], SIZE_4KB);
      if (Bars[Bar] == 0) {
        Sizes[Bar] = 0;
      }
    }
  }

  Status = Ps4InstallPciIoShim (mEcam, (UINT8)Device, (UINT8)Function, Bars, Sizes, &Handle);
  DEBUG ((DEBUG_INFO, "Ps4SbIo: 00:%02x.%x virtio-net (QEMU) via PciIo shim: %r\n", Device, Function, Status));
  return EFI_ERROR (Status) ? 0 : 1;
}

STATIC
UINTN
RegisterGeneric (
  VOID
  )
{
  UINTN   Device;
  UINTN   Function;
  UINTN   Index;
  UINTN   Count;
  UINT32  Id;
  UINT32  Class;

  Count = 0;
  for (Device = 0; Device < 32; Device++) {
    for (Function = 0; Function < 8; Function++) {
      Id = CfgRead32 (Device, Function, 0x00);
      if (((Id & 0xFFFF) == 0xFFFF) || ((Id & 0xFFFF) == 0)) {
        if (Function == 0) {
          break;
        }

        continue;
      }

      if ((Id & 0xFFFF) != SONY_VENDOR) {                  // Sony functions: device 0x14 table only
        Class = CfgRead32 (Device, Function, 0x08) >> 8;
        if (((Id & 0xFFFF) == 0x1AF4) && ((Class >> 16) == 0x02) && UsableAsLeft (Device, Function, "virtio-net")) {
          Count += RegisterVirtioNet (Device, Function);
        }
        for (Index = 0; Index < ARRAY_SIZE (mGeneric); Index++) {
          if ((Class == mGeneric[Index].ClassCode) && UsableAsLeft (Device, Function, mGeneric[Index].Name)) {
            Count += Register (
                       mGeneric[Index].Type,
                       MemBar (Device, Function, mGeneric[Index].Bar),
                       mGeneric[Index].Size,
                       Device,
                       Function,
                       mGeneric[Index].Bar,
                       mGeneric[Index].Name
                       );
          }
        }
      }

      if ((Function == 0) && ((CfgRead32 (Device, 0, 0x0C) & (BIT7 << 16)) == 0)) {
        break;                                             // single-function device
      }
    }
  }

  return Count;
}

EFI_STATUS
EFIAPI
Ps4SouthbridgeIoEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  VOID                   *Hob;
  CONST ACPI_BOARD_INFO  *Board;
  UINTN                  Count;

  Hob = GetFirstGuidHob (&gUefiAcpiBoardInfoGuid);
  if (Hob == NULL) {
    DEBUG ((DEBUG_ERROR, "Ps4SbIo: no ACPI board info HOB (no MCFG): no I/O devices registered\n"));
    return EFI_NOT_FOUND;
  }

  Board = GET_GUID_HOB_DATA (Hob);
  mEcam = (UINTN)Board->PcieBaseAddress;
  if ((mEcam == 0) || (Board->PcieBaseSize < SIZE_1MB)) {
    DEBUG ((DEBUG_ERROR, "Ps4SbIo: ECAM 0x%lx size 0x%lx unusable: no I/O devices registered\n", Board->PcieBaseAddress, Board->PcieBaseSize));
    return EFI_NOT_FOUND;
  }

  Count  = RegisterSony ();
  Count += RegisterGeneric ();
  DEBUG ((DEBUG_INFO, "Ps4SbIo: ECAM 0x%lx, %u controller(s) registered (config space read only)\n", (UINT64)mEcam, Count));
  return EFI_SUCCESS;
}
