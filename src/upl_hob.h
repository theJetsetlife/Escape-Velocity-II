/*
 * upl_hob.h — the EDK2 Universal Payload handoff contract, as clean C.
 * Clean-room: every struct/GUID below is transcribed from PRIMARY edk2 source
 * (see tags), never from the book or the old shim. Ledger: G1/G2/G3 GREEN.
 *
 * Primary sources (uefi-shell-lab/edk2):
 *   MdePkg/Include/Pi/PiHob.h                         generic hdr, PHIT, resource, FV, CPU
 *   MdePkg/Include/Guid/GraphicsInfoHob.h             graphics info hob + GUID
 *   MdeModulePkg/Include/UniversalPayload/ (.h)       generic hdr, ExtraData, AcpiTable
 *   MdeModulePkg/MdeModulePkg.dec / UefiPayloadPkg    GUID values
 *   UefiPayloadEntry/UniversalPayloadEntry.c          entry ABI (RCX=HobList), required HOBs
 */
#ifndef UPL_HOB_H
#define UPL_HOB_H
#include "ps4.h"

typedef u64 efi_phys;   /* EFI_PHYSICAL_ADDRESS */

typedef struct { u32 d1; u16 d2; u16 d3; u8 d4[8]; } efi_guid;   /* EFI_GUID, 16 B */

/* ---- PiHob.h: generic header + HOB type constants (verified values) ---- */
typedef struct { u16 type; u16 length; u32 reserved; } hob_header;   /* 8 B */
#define HOB_TYPE_HANDOFF              0x0001
#define HOB_TYPE_MEMORY_ALLOCATION    0x0002
#define HOB_TYPE_RESOURCE_DESCRIPTOR  0x0003
#define HOB_TYPE_GUID_EXTENSION       0x0004
#define HOB_TYPE_FV                   0x0005
#define HOB_TYPE_CPU                  0x0006
/* EFI_HOB_CPU (MdePkg Pi/PiHob.h). Required by DxeCore Gcd.c CoreInitializeGcdServices; the UPL
 * entry does not build it (only the non-UPL UefiPayloadEntry does), so the bootloader must
 * (HW run 20:41 UTC: ASSERT Gcd.c(2612) CpuHob != NULL). */
#define HOB_TYPE_END_OF_HOB_LIST      0xFFFF

/* PHIT — EFI_HOB_HANDOFF_INFO_TABLE (56 B) */
typedef struct {
	hob_header header;            /* type = HOB_TYPE_HANDOFF */
	u32        version;
	u32        boot_mode;
	efi_phys   memory_top;
	efi_phys   memory_bottom;
	efi_phys   free_memory_top;
	efi_phys   free_memory_bottom;
	efi_phys   end_of_hob_list;
} hob_handoff_info;
#define EFI_HOB_HANDOFF_TABLE_VERSION 0x0009

/* EFI_HOB_RESOURCE_DESCRIPTOR (48 B) */
typedef struct {
	hob_header header;            /* type = HOB_TYPE_RESOURCE_DESCRIPTOR */
	efi_guid   owner;
	u32        resource_type;
	u32        resource_attribute;
	efi_phys   physical_start;
	u64        resource_length;
} hob_resource_descriptor;
/* EFI_RESOURCE_TYPE */
#define EFI_RESOURCE_SYSTEM_MEMORY          0
#define EFI_RESOURCE_MEMORY_MAPPED_IO       1
#define EFI_RESOURCE_MEMORY_RESERVED        5
/* EFI_RESOURCE_ATTRIBUTE (the "present+initialized+tested+cacheable" set DXE wants) */
#define EFI_RESOURCE_ATTRIBUTE_PRESENT               0x00000001
#define EFI_RESOURCE_ATTRIBUTE_INITIALIZED           0x00000002
#define EFI_RESOURCE_ATTRIBUTE_TESTED                0x00000004
#define EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE           0x00000008
#define EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE     0x00000010
#define EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE 0x00000020
#define EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE  0x00000040

/* EFI_HOB_FIRMWARE_VOLUME (24 B) */
typedef struct { hob_header header; efi_phys base_address; u64 length; } hob_firmware_volume;

/* EFI_HOB_CPU (16 B) */
typedef struct { hob_header header; u8 size_of_memory_space; u8 size_of_io_space; u8 reserved[6]; } hob_cpu;

/* EFI_HOB_GUID_TYPE (24 B header, then data) */
typedef struct { hob_header header; efi_guid name; } hob_guid_type;

/* ---- MdeModulePkg/Include/UniversalPayload/UniversalPayload.h ---- */
typedef struct { u8 revision; u8 reserved; u16 length; } upl_generic_header;   /* 4 B */

/* ExtraData.h — REQUIRED HOB; Entry[0].identifier must be "uefi_fv" (= DXE FV) */
typedef struct { char identifier[16]; efi_phys base; u64 size; } upl_extra_data_entry;   /* 32 B */
typedef struct { upl_generic_header header; u32 count; upl_extra_data_entry entry[]; } upl_extra_data;

/* AcpiTable.h — optional */
/* edk2 MdeModulePkg/Include/UniversalPayload/AcpiTable.h is #pragma pack(1): Rsdp at offset 4,
 * 12 bytes. Unpacked (offset 8) EDK II read 0xE0000 << 32 and faulted (HW run 19:58 UTC). */
typedef struct __attribute__((packed)) { upl_generic_header header; efi_phys rsdp; } upl_acpi_table;

/* ---- MdePkg/Include/Guid/GraphicsInfoHob.h ---- */
/* EFI_GRAPHICS_OUTPUT_MODE_INFORMATION (UEFI spec GOP) */
typedef struct { u32 red, green, blue, reserved; } efi_pixel_bitmask;
typedef struct {
	u32 version;
	u32 horizontal_resolution;
	u32 vertical_resolution;
	u32 pixel_format;            /* 1 = PixelBlueGreenRedReserved8BitPerColor (amdgpu BGRX) */
	efi_pixel_bitmask pixel_information;
	u32 pixels_per_scan_line;
} efi_gop_mode_info;
#define PixelBlueGreenRedReserved8BitPerColor 1
typedef struct {
	efi_phys          frame_buffer_base;    /* RUNTIME: amdgpu scanout phys (ledger H1) */
	u32               frame_buffer_size;
	efi_gop_mode_info mode;                  /* 1920x1080, pps=1920, BGRX (ledger H1) */
} efi_graphics_info_hob;

/* ---- GUID values (primary .dec; do not alter) ---- */
#define GUID_UPL_EXTRA_DATA   { 0x15a5baf6, 0x1c91, 0x467d, { 0x9d,0xfb,0x31,0x9d,0x17,0x8d,0x4b,0xb4 } }
#define GUID_UPL_ACPI_TABLE   { 0x9f9a9506, 0x5597, 0x4515, { 0xba,0xb6,0x8b,0xcd,0xe7,0x84,0xba,0x87 } }
#define GUID_UPL_PCI_RB_INFO  { 0xec4ebacb, 0x2638, 0x416e, { 0xbe,0x80,0xe5,0xfa,0x4b,0x51,0x19,0x01 } }
#define GUID_EFI_GRAPHICS_INFO { 0x39f62cce, 0x6825, 0x4669, { 0xbb,0x56,0x54,0x1a,0xba,0x75,0x3a,0x07 } }

/* Ours (Ps4Pkg/Include/Guid/Ps4DirectbootInfo.h must match): what the directboot stage
 * measured at takeover, for Ps4Pkg drivers (RTC seed, GOP, diagnostics). */
#define GUID_PS4_DIRECTBOOT_INFO { 0x019f07b6, 0x0a23, 0x4609, { 0xb8,0x01,0xa2,0x04,0x0c,0x17,0x3a,0x21 } }
#define PS4_DIRECTBOOT_INFO_VERSION 1
typedef struct {
	u32 version, size;
	u64 unix_time;                 /* OrbisOS CLOCK_REALTIME when the stage armed */
	u64 tsc_hz;                    /* TSC frequency measured under OrbisOS (0 = unknown) */
	u64 fb_base;                   /* scanout surface (CPU phys) at takeover */
	u32 fb_width, fb_height, fb_pitch_bytes, fb_crtc;
	u64 kbase;                     /* OrbisOS kernel base (diagnostics) */
	u64 boot_block, boot_block_size; /* reserved: our GDT/page tables/stack */
	u32 halted_aps, flags;
} ps4_directboot_info;

/* Compile-time size checks against the PI/UPL spec (catch an ABI drift at build) */
_Static_assert(sizeof(efi_guid) == 16, "efi_guid");
_Static_assert(sizeof(hob_header) == 8, "hob_header");
_Static_assert(sizeof(hob_handoff_info) == 56, "PHIT");
_Static_assert(sizeof(hob_resource_descriptor) == 48, "resource descriptor");
_Static_assert(sizeof(hob_firmware_volume) == 24, "fv hob");
_Static_assert(sizeof(hob_cpu) == 16, "cpu hob");
_Static_assert(sizeof(upl_generic_header) == 4, "upl generic header");
_Static_assert(sizeof(upl_extra_data_entry) == 32, "extra data entry");
_Static_assert(sizeof(upl_acpi_table) == 12, "UNIVERSAL_PAYLOAD_ACPI_TABLE is packed: 4 + 8");
_Static_assert(sizeof(hob_cpu) == 16, "EFI_HOB_CPU = 8 + 1 + 1 + 6");

/* MdeModulePkg/Include/UniversalPayload/PciRootBridges.h (pack(1)). Without this HOB the UPL's
 * PciHostBridgeLib runs ScanForRootBridges over buses 0..255; Sony's MCFG decodes only 0..0x3F,
 * so buses >= 64 would read the IOMMU / LAPIC / other MMIO as config space. */
#define GUID_UPL_PCI_ROOT_BRIDGES { 0xec4ebacb, 0x2638, 0x416e, { 0xbe,0x80,0xe5,0xfa,0x4b,0x51,0x19,0x01 } }
typedef struct __attribute__((packed)) { u64 base, limit, translation; } upl_pci_aperture;
typedef struct __attribute__((packed)) {
	u32 segment; u64 supports, attributes; u8 dma_above_4g, no_extended_config_space;
	u64 allocation_attributes;
	upl_pci_aperture bus, io, mem, mem_above_4g, pmem, pmem_above_4g;
	u32 hid, uid;
} upl_pci_root_bridge;
typedef struct __attribute__((packed)) {
	upl_generic_header header; u8 resource_assigned, count; upl_pci_root_bridge root_bridge[1];
} upl_pci_root_bridges;
_Static_assert(sizeof(upl_pci_aperture) == 24, "aperture");
_Static_assert(sizeof(upl_pci_root_bridge) == 4 + 8 + 8 + 1 + 1 + 8 + 6 * 24 + 4 + 4, "UNIVERSAL_PAYLOAD_PCI_ROOT_BRIDGE");
_Static_assert(__builtin_offsetof(upl_pci_root_bridges, root_bridge) == 6, "UNIVERSAL_PAYLOAD_PCI_ROOT_BRIDGES header");
_Static_assert(__builtin_offsetof(upl_acpi_table, rsdp) == 4, "Rsdp at offset 4 (pack(1))");
_Static_assert(__builtin_offsetof(upl_extra_data, count) == 4 && __builtin_offsetof(upl_extra_data, entry) == 8,
               "UNIVERSAL_PAYLOAD_EXTRA_DATA layout");
_Static_assert(sizeof(efi_graphics_info_hob) == 48, "EFI_PEI_GRAPHICS_INFO_HOB = 8 + 4 + 36");

#endif
