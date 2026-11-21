/** @file
  PS4 directboot info HOB: what the directboot stage measured at takeover.
  Producer: rebuild/src/upl_load.c (ps4_directboot_info in src/upl_hob.h - keep in sync).
**/

#ifndef PS4_DIRECTBOOT_INFO_H_
#define PS4_DIRECTBOOT_INFO_H_

#define PS4_DIRECTBOOT_INFO_GUID \
  { 0x019f07b6, 0x0a23, 0x4609, { 0xb8, 0x01, 0xa2, 0x04, 0x0c, 0x17, 0x3a, 0x21 } }

#define PS4_DIRECTBOOT_INFO_VERSION  1

#pragma pack(1)
typedef struct {
  UINT32    Version;
  UINT32    Size;
  UINT64    UnixTime;          ///< OrbisOS CLOCK_REALTIME when the stage armed
  UINT64    TscHz;             ///< TSC frequency measured under OrbisOS (0 = unknown)
  UINT64    FbBase;            ///< scanout surface (CPU physical) at takeover
  UINT32    FbWidth;
  UINT32    FbHeight;
  UINT32    FbPitchBytes;
  UINT32    FbCrtc;
  UINT64    KernelBase;        ///< OrbisOS kernel base (diagnostics)
  UINT64    BootBlock;         ///< reserved: stage GDT / page tables
  UINT64    BootBlockSize;
  UINT32    HaltedAps;
  UINT32    Flags;             ///< PS4_DIRECTBOOT_FLAG_*
} PS4_DIRECTBOOT_INFO;
#pragma pack()

/// QEMU harness only: install the PS4 DSDT although the tables are not Sony's (tests Ps4AcpiDxe).
#define PS4_DIRECTBOOT_FLAG_FORCE_PS4_DSDT  BIT0
/// QEMU harness only: reduce the MADT to what Sony ships (local APICs only, no IOAPIC/ISO/NMI, PCAT_COMPAT 0).
#define PS4_DIRECTBOOT_FLAG_SONY_MADT       BIT1
/// with SONY_MADT, harness experiments: keep PCAT_COMPAT = 1 / keep the real IOAPIC entries.
#define PS4_DIRECTBOOT_FLAG_KEEP_PCAT       BIT2
#define PS4_DIRECTBOOT_FLAG_KEEP_IOAPIC     BIT3

extern EFI_GUID  gPs4DirectbootInfoGuid;

#endif
