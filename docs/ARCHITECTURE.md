# Architecture

## Boot chain
```
PS4 browser ── WebKitty page (PSFree + Lapse kernel exploit, 9.00 kpatch: sysent[11] = kexec gadget)
   └─ GoldHEN slot payload = src/directboot/directboot.bin  (userland thread, entry at offset 0)
        user.c   connect to PC receiver (PC_IP:PC_PORT), fetch out/upl-ps4.elf ("DBUPLGET"), sample TSC
        arm.c    syscall 11 → ring 0, one syscall per step, each reported: 9.00 gates, RWX patches,
                 kernel copy of the blob, contiguous staging RAM (ELF copy, 1 MiB region, boot block),
                 scanout lookup, install hook on icc_query_nowait  →  kern_reboot()
   OrbisOS shutdown reaches the ICC hook:
        hook.c   rendezvous all CPUs, park the 7 APs, LAPIC quiet, scanout + VRAM MTRRs (UC),
                 quiesce (IOMMU, Aeolia MSIs, HPET, GPU engines, HDMI audio DMA), wlanbt(2),
                 configure_vram (2 GiB carve at 4 GiB), memory map (memmap.c), own page tables + GDT,
                 boot-log ring, upl_load()
   src/upl_load.c  load the Universal Payload ELF, apply relocations, build HOBs, jump
   EDK II (UefiPayloadPkg + Ps4Pkg) → DXE → BDS → UEFI Shell / boot option
```
The stage refuses to run on anything but retail 9.00 (kernel signature gates) and halts with a red square if
`upl_load` fails. On-screen progress squares: `src/directboot/README.md`.

## Handoff interfaces (stage → EDK II)
| Interface | Where | Content |
|---|---|---|
| Universal Payload HOBs | `src/upl_hob.h` | memory map (resource descriptors), ACPI RSDP (Sony's tables), graphics info (scanout), serial info, PCI root bridge, CPU |
| `PS4_DIRECTBOOT_INFO` GUID HOB | `Ps4Pkg/Include/Guid/Ps4DirectbootInfo.h` | unix time, TSC Hz, framebuffer, kernel base, boot block, halted APs, `Flags` (harness test bits) |
| fb console descriptor v3 | phys `0x8000`, `Ps4Pkg/Include/Ps4FbConsole.h` | text console on the scanout for DEBUG output before GOP; v3 adds the boot-log ring (1 MiB at the end of the VRAM carve) |

## Memory (PS4 Fat, 8 GiB shared)
- TOP_MEM 2 GiB, TOM2 0x27F000000. Below 2 GiB: RAM minus what OrbisOS/handoff still uses (rsv list), low 1 MiB
  reserved except the legacy VGA hole 0xA0000–0xBFFFF (a map entry there breaks Windows' root-bridge checks).
- 0x100000000–0x17FFFFFFF: 2 GiB VRAM carve (GPU MC programmed like the PS4 Linux loader), reserved, UC.
- 0x180000000–0x273FFFFFF: released to EDK II as RAM (~3.8 GiB) → ~5.2 GiB usable total.
- 0x274000000+: OrbisOS device-shared, reserved. Anything not provably RAM is reserved (fail closed).
- Southbridge devices are 31-bit DMA masters: the PciIo shim bounces buffers above 2 GiB.

## Ps4Pkg map
| Component | Purpose |
|---|---|
| `mkplatform.py`, `build.sh` | generate `Ps4Payload.dsc/.fdf` from upstream UefiPayloadPkg by exact-match edits; build the UPL |
| `Ps4GopDxe` | GOP on the scanout described by the stage (no PCI enumeration) |
| `Ps4SouthbridgeIoDxe` (+ `Ps4PciIoShim`) | PciIo for Aeolia xHCI/AHCI/SDHCI/GbE without PciBusDxe; config space read-only; bounce DMA |
| `Ps4YukonDxe` | Marvell Yukon Ethernet (port of OpenPlatformPkg MarvellYukonDxe) |
| `Ps4Ds4Dxe` | USB DualShock 4 as a UEFI keyboard |
| `Ps4ApicFixupDxe` | clean the inherited BSP local APIC (stale ISR/IRR/TPR) |
| `Ps4RtcDxe`, `Ps4RngDxe` | RTC from OrbisOS time + TSC (no CMOS); RNG protocol (non-cryptographic, no RDRAND on Jaguar) |
| `Ps4NtfsDxe` | read-only NTFS (OpenNtfsDxe port) |
| `Ps4LogDxe` | `ps4log` Shell command + ReadyToBoot auto-save |
| `Ps4AcpiDxe` (+ `Ps4Dsdt.asl`) | FACS into ACPI NVS; own DSDT on Sony tables (root bridge windows measured at boot); RAM IOAPIC when the MADT has none |
| `Library/Ps4FbSerialPortLib` | DEBUG output to the fb console + boot-log ring (+ UART mirror) |
| `Library/Ps4UartLib`, `Ps4UartSerialPortLib` | southbridge UART (115200 8N1) |
| `Library/Ps4TscTimerLib`, `Ps4LocalApicLib`, `Ps4ResetSystemLib` | TSC timer (no CPUID 0x15 on Jaguar), LAPIC lib, reset (parks the CPU: no reset register) |

## QEMU harness (qemu/)
`stub.S/stub.c` (multiboot, 32-bit) collects e820/framebuffer/RSDP into `hinfo` → `h64.c` (64-bit) plays the
stage: reserved ranges, memory map through the same `memmap.c`, fb console v3, optional stale LAPIC, VRAM carve,
then the same `upl_load.c`. So the loader, memory-map rules and all of EDK II are exercised on every run.
