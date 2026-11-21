/** @file
  Ps4PciIoShim - minimal PciIo over real config space for one live southbridge function
  (see Ps4PciIoShim.c for the write rules).
**/

#ifndef PS4_PCI_IO_SHIM_H_
#define PS4_PCI_IO_SHIM_H_

#include <Uefi.h>

/** Install PciIo + DevicePath (PciRoot(0)/Pci(Device,Function)) on a new handle. */
EFI_STATUS
Ps4InstallPciIoShim (
  IN  UINTN        Ecam,
  IN  UINT8        Device,
  IN  UINT8        Function,
  IN  CONST UINT64 *Bars,
  IN  CONST UINT64 *BarSizes,
  OUT EFI_HANDLE   *Handle
  );

#endif
