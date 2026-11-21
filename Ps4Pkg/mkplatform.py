#!/usr/bin/env python3
"""Generate Ps4Pkg/Ps4Payload.dsc + .fdf from the pinned upstream UefiPayloadPkg files.

Upstream edk2 is never edited. Every PS4 difference is listed here, each edit must match
exactly once, and the generated files carry a header naming the upstream commit.

  python3 -I Ps4Pkg/mkplatform.py <edk2-dir>
"""
import os, subprocess, sys

EDK2 = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "edk2"))
OUT = os.path.dirname(os.path.abspath(__file__))

DSC_EDITS = [
    ("MpInitLib: uniprocessor (MpInitLibUp). DxeMpInitLib broadcasts INIT-SIPI-SIPI to every AP even with "
     "MAX_LOGICAL_PROCESSORS=1 and sizes AP stacks/CpuData for 1 CPU: on PS4 that restarts the 7 parked "
     "cores inside EDK II (HW deaths 32.96/37.47/39.5 s at 3 different steps; QEMU -smp 8 hangs)",
     "  MpInitLib|UefiCpuPkg/Library/MpInitLib/DxeMpInitLib.inf",
     "  MpInitLib|UefiCpuPkg/Library/MpInitLibUp/MpInitLibUp.inf", 2),
    # (why, old, new)
    ("platform FDF",
     "  FLASH_DEFINITION                    = UefiPayloadPkg/UefiPayloadPkg.fdf",
     "  FLASH_DEFINITION                    = Ps4Pkg/Ps4Payload.fdf"),
    ("TimerLib: Jaguar has no CPUID 0x15; PM timer not trusted -> TSC (Ps4TscTimerLib)",
     "  TimerLib|UefiCpuPkg/Library/CpuTimerLib/BaseCpuTimerLib.inf",
     "  TimerLib|Ps4Pkg/Library/Ps4TscTimerLib/Ps4TscTimerLib.inf"),
    ("TimerLib (non-CPU-timer branch) -> TSC as well",
     "  TimerLib|UefiPayloadPkg/Library/AcpiTimerLib/AcpiTimerLib.inf",
     "  TimerLib|Ps4Pkg/Library/Ps4TscTimerLib/Ps4TscTimerLib.inf"),
    ("RTC: no CMOS at 0x70/0x71 -> Ps4RtcDxe (Aeolia RTC counter as EFI time; ACPI TAD + no-CMOS FADT via Ps4AcpiDxe)",
     "  PcAtChipsetPkg/PcatRealTimeClockRuntimeDxe/PcatRealTimeClockRuntimeDxe.inf",
     "  Ps4Pkg/Ps4RtcDxe/Ps4RtcDxe.inf"),
    ("RTC: drop the CMOS port PCDs (no module declares them once PcatRealTimeClock is gone)",
     "  gPcAtChipsetPkgTokenSpaceGuid.PcdRtcIndexRegister|$(RTC_INDEX_REGISTER)\n"
     "  gPcAtChipsetPkgTokenSpaceGuid.PcdRtcTargetRegister|$(RTC_TARGET_REGISTER)\n",
     ""),
    ("Serial: no legacy UART -> DEBUG() text on the framebuffer (Ps4FbSerialPortLib), coreboot variant",
     "  SerialPortLib|UefiPayloadPkg/Library/CbSerialPortLib/CbSerialPortLib.inf",
     "  SerialPortLib|Ps4Pkg/Library/Ps4FbSerialPortLib/Ps4FbSerialPortLib.inf"),
    ("Serial: HOB-based DXE variant (2 places)",
     "SerialPortLib|UefiPayloadPkg/Library/BaseSerialPortLibHob/DxeBaseSerialPortLibHob.inf",
     "SerialPortLib|Ps4Pkg/Library/Ps4FbSerialPortLib/Ps4FbSerialPortLib.inf", 2),
    ("Serial: 16550 variant",
     "  SerialPortLib|MdeModulePkg/Library/BaseSerialPortLib16550/BaseSerialPortLib16550.inf",
     "  SerialPortLib|Ps4Pkg/Library/Ps4FbSerialPortLib/Ps4FbSerialPortLib.inf"),
    ("Serial: HOB-based base variant (UPL entry)",
     "  SerialPortLib|UefiPayloadPkg/Library/BaseSerialPortLibHob/BaseSerialPortLibHob.inf",
     "  SerialPortLib|Ps4Pkg/Library/Ps4FbSerialPortLib/Ps4FbSerialPortLib.inf"),
    ("DebugLib: DXE core/drivers print directly through SerialPortLib (fb console); the status-code\n"
     "##        route is serial-gated and silent with no UART (HW run 20:14 UTC: no DXE output)",
     "  DebugLib|MdeModulePkg/Library/PeiDxeDebugLibReportStatusCode/PeiDxeDebugLibReportStatusCode.inf",
     "  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf", 2),
    ("ResetSystemLib: Sony FADT has no reset register (upstream lib ASSERTs, HW run 22:18 UTC)",
     "  ResetSystemLib|UefiPayloadPkg/Library/ResetSystemLib/ResetSystemLib.inf",
     "  ResetSystemLib|Ps4Pkg/Library/Ps4ResetSystemLib/Ps4ResetSystemLib.inf"),
    ("LocalApicLib: LINT0 stays masked in ProgramVirtualWireMode (no 8259 on PS4; HANDOFF_AUDIT_2 #1)",
     "  LocalApicLib|UefiCpuPkg/Library/BaseXApicX2ApicLib/BaseXApicX2ApicLib.inf",
     "  LocalApicLib|Ps4Pkg/Library/Ps4LocalApicLib/Ps4LocalApicLib.inf"),
    ("GOP: scanout is in DRAM outside the GPU BARs -> Ps4GopDxe (binds the Liverpool PciIo)",
     "  UefiPayloadPkg/GraphicsOutputDxe/GraphicsOutputDxe.inf",
     "  Ps4Pkg/Ps4GopDxe/Ps4GopDxe.inf"),
    ("LAPIC: clear in-service/pending vectors left by the takeover (HW 07:35 UTC stopped at BdsWait(3), timer 0x20 blocked) -> Ps4ApicFixupDxe",
     "  Ps4Pkg/Ps4GopDxe/Ps4GopDxe.inf",
     "  Ps4Pkg/Ps4GopDxe/Ps4GopDxe.inf\n  Ps4Pkg/Ps4ApicFixupDxe/Ps4ApicFixupDxe.inf"),
    ("I/O: southbridge controllers get PciIo without PCI enumeration (config reads only) -> "
     "Ps4SouthbridgeIoDxe + NonDiscoverablePciDeviceDxe; registration library",
     "  LocalApicLib|Ps4Pkg/Library/Ps4LocalApicLib/Ps4LocalApicLib.inf",
     "  LocalApicLib|Ps4Pkg/Library/Ps4LocalApicLib/Ps4LocalApicLib.inf\n"
     "  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf"),
    ("I/O: components",
     "  Ps4Pkg/Ps4ApicFixupDxe/Ps4ApicFixupDxe.inf",
     "  Ps4Pkg/Ps4ApicFixupDxe/Ps4ApicFixupDxe.inf\n"
     "  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf\n"
     "  Ps4Pkg/Ps4SouthbridgeIoDxe/Ps4SouthbridgeIoDxe.inf\n"
     "  Ps4Pkg/Ps4Ds4Dxe/Ps4Ds4Dxe.inf\n"
     "  Ps4Pkg/Ps4YukonDxe/Ps4YukonDxe.inf\n"
     "  Ps4Pkg/Ps4RngDxe/Ps4RngDxe.inf\n"
     "  SecurityPkg/Hash2DxeCrypto/Hash2DxeCrypto.inf\n"
     "  Ps4Pkg/Ps4NtfsDxe/Ps4NtfsDxe.inf\n"
     "  Ps4Pkg/Ps4LogDxe/Ps4LogDxe.inf\n"
     "  Ps4Pkg/Ps4AcpiDxe/Ps4AcpiDxe.inf\n"
     "  OvmfPkg/Virtio10Dxe/Virtio10.inf {\n"
     "    <LibraryClasses>\n"
     "      PciCapLib|OvmfPkg/Library/BasePciCapLib/BasePciCapLib.inf\n"
     "      PciCapPciIoLib|OvmfPkg/Library/UefiPciCapPciIoLib/UefiPciCapPciIoLib.inf\n"
     "  }\n"
     "  OvmfPkg/VirtioNetDxe/VirtioNet.inf {\n"
     "    <LibraryClasses>\n"
     "      VirtioLib|OvmfPkg/Library/VirtioLib/VirtioLib.inf\n"
     "      OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf\n"
     "  }"),
    ("UART: Ps4UartLib (southbridge UART, COM1 off the PS4) for the DEBUG mirror",
     "  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf",
     "  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf\n"
     "  Ps4UartLib|Ps4Pkg/Library/Ps4UartLib/Ps4UartLib.inf"),
    ("UART: SerialDxe on the UART only (TerminalDxe = Shell over serial; the screen keeps GraphicsConsole)",
     "  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf\n",
     "  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf {\n"
     "    <LibraryClasses>\n"
     "      SerialPortLib|Ps4Pkg/Library/Ps4UartSerialPortLib/Ps4UartSerialPortLib.inf\n"
     "  }\n"),
]
FDF_EDITS = [
    ("PCI bus: no PciBusDxe (HW 07:21 UTC: died enumerating the live GPU 00:01.0); GOP comes from Ps4GopDxe without PCI",
     "INF MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf",
     "# INF MdeModulePkg/Bus/Pci/PciBusDxe/PciBusDxe.inf  (removed for PS4: see Ps4Pkg/Ps4GopDxe/Ps4GopDxe.c)"),
    ("RTC", "  INF PcAtChipsetPkg/PcatRealTimeClockRuntimeDxe/PcatRealTimeClockRuntimeDxe.inf",
            "  INF Ps4Pkg/Ps4RtcDxe/Ps4RtcDxe.inf"),
    ("GOP", "INF UefiPayloadPkg/GraphicsOutputDxe/GraphicsOutputDxe.inf",
            "INF Ps4Pkg/Ps4GopDxe/Ps4GopDxe.inf"),
    ("LAPIC fixup", "INF Ps4Pkg/Ps4GopDxe/Ps4GopDxe.inf",
                    "INF Ps4Pkg/Ps4GopDxe/Ps4GopDxe.inf\n  INF Ps4Pkg/Ps4ApicFixupDxe/Ps4ApicFixupDxe.inf"),
    ("I/O", "INF Ps4Pkg/Ps4ApicFixupDxe/Ps4ApicFixupDxe.inf",
            "INF Ps4Pkg/Ps4ApicFixupDxe/Ps4ApicFixupDxe.inf\n"
            "  INF MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf\n"
            "  INF Ps4Pkg/Ps4SouthbridgeIoDxe/Ps4SouthbridgeIoDxe.inf\n"
            "  INF Ps4Pkg/Ps4Ds4Dxe/Ps4Ds4Dxe.inf\n"
            "  INF Ps4Pkg/Ps4YukonDxe/Ps4YukonDxe.inf\n"
            "  INF Ps4Pkg/Ps4RngDxe/Ps4RngDxe.inf"),
    ("NETWORKFV left empty: the directboot loader does not pass network_fv; network goes into PS4COMPFV",
     "#\n# UEFI network modules\n#\n!include NetworkPkg/Network.fdf.inc\n!endif\n",
     "#\n# UEFI network modules: carried by PS4COMPFV inside DXEFV on the PS4\n#\n!endif\n"),
    ("Shell + TFTP command move into PS4COMPFV (LZMA)",
     "!if $(SHELL_TYPE) == BUILD_SHELL\nINF ShellPkg/DynamicCommand/TftpDynamicCommand/TftpDynamicCommand.inf\n",
     "!if $(SHELL_TYPE) == BUILD_SHELL\n"),
    ("DXEFV carries PS4COMPFV as an LZMA-compressed FV image",
     "INF ShellPkg/Application/Shell/Shell.inf\n!endif\n",
     "!endif\nFILE FV_IMAGE = 4DA7A673-797A-4D2E-94C5-83419552F975 {\n"
     "  SECTION GUIDED EE4E5898-3914-4259-9D6E-DC7BD79403CF PROCESSING_REQUIRED = TRUE {\n"
     "    SECTION FV_IMAGE = PS4COMPFV\n"
     "  }\n"
     "}\n"),
    ("PS4COMPFV definition (before BDSFV: inside the UNIVERSAL_PAYLOAD block)", "[FV.BDSFV]\n", "################################################################################\n# PS4: LZMA-compressed FV inside DXEFV, decompressed by the DXE core (LzmaCustomDecompressLib).\n# Shell, TFTP command and the network stack: keeps the UPL ELF small (the stage needs one\n# contiguous block for it) and puts the network modules in an FV the directboot loader hands\n# over (src/upl_load.c passes uefi_fv and bds_fv only).\n[FV.PS4COMPFV]\nFvNameGuid         = 1914E312-3464-4B2E-A2F1-4555EC39DC17\nBlockSize          = $(FD_BLOCK_SIZE)\nFvForceRebase      = FALSE\nFvAlignment        = 16\nERASE_POLARITY     = 1\nMEMORY_MAPPED      = TRUE\nSTICKY_WRITE       = TRUE\nLOCK_CAP           = TRUE\nLOCK_STATUS        = TRUE\nWRITE_DISABLED_CAP = TRUE\nWRITE_ENABLED_CAP  = TRUE\nWRITE_STATUS       = TRUE\nWRITE_LOCK_CAP     = TRUE\nWRITE_LOCK_STATUS  = TRUE\nREAD_DISABLED_CAP  = TRUE\nREAD_ENABLED_CAP   = TRUE\nREAD_STATUS        = TRUE\nREAD_LOCK_CAP      = TRUE\nREAD_LOCK_STATUS   = TRUE\n\n!if $(SHELL_TYPE) == BUILD_SHELL\nINF ShellPkg/DynamicCommand/TftpDynamicCommand/TftpDynamicCommand.inf\nINF ShellPkg/Application/Shell/Shell.inf\n!endif\n!if $(NETWORK_DRIVER_ENABLE) == TRUE\n!include NetworkPkg/Network.fdf.inc\nINF SecurityPkg/Hash2DxeCrypto/Hash2DxeCrypto.inf\nINF OvmfPkg/Virtio10Dxe/Virtio10.inf\nINF OvmfPkg/VirtioNetDxe/VirtioNet.inf\n!endif\nINF Ps4Pkg/Ps4NtfsDxe/Ps4NtfsDxe.inf\nINF Ps4Pkg/Ps4LogDxe/Ps4LogDxe.inf\nINF Ps4Pkg/Ps4AcpiDxe/Ps4AcpiDxe.inf\n\n[FV.BDSFV]\n"),
]


def apply(path, edits, out):
    s = open(path).read()
    for e in edits:
        why, old, new = e[:3]
        want = e[3] if len(e) > 3 else 1
        n = s.count(old)
        if n != want:
            raise SystemExit("%s: expected %d match(es) for [%s], found %d" % (path, want, why, n))
        s = s.replace(old, new)
    rev = subprocess.run(["git", "-C", EDK2, "log", "-1", "--format=%H %cs"], capture_output=True, text=True).stdout.strip()
    head = "## GENERATED by Ps4Pkg/mkplatform.py from upstream %s @ %s - do not edit.\n" % (
        os.path.relpath(path, EDK2), rev)
    head += "".join("##   PS4: %s\n" % e[0] for e in edits)
    open(out, "w").write(head + s)
    print("wrote", out)


def make_font():
    """8x19 glyphs 0x20..0x7E from upstream GraphicsConsoleDxe/LaffStd.c (BSD-2-Clause-Patent)."""
    import re
    src = os.path.join(EDK2, "MdeModulePkg/Universal/Console/GraphicsConsoleDxe/LaffStd.c")
    g = {}
    for m in re.finditer(r"\{\s*0x([0-9a-fA-F]{4}),\s*0x[0-9a-fA-F]+,\s*\{([^}]*)\}", open(src).read()):
        cp = int(m.group(1), 16)
        b = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2))]
        if 0x20 <= cp <= 0x7e and len(b) == 19:
            g[cp] = b
    missing = [hex(c) for c in range(0x20, 0x7f) if c not in g]
    if missing:
        raise SystemExit("LaffStd.c: missing glyphs " + " ".join(missing))
    out = os.path.join(OUT, "Library", "Ps4FbSerialPortLib", "Font8x19.h")
    with open(out, "w") as f:
        f.write("// GENERATED by Ps4Pkg/mkplatform.py from upstream %s - do not edit.\n" % os.path.relpath(src, EDK2))
        f.write("STATIC CONST UINT8 mPs4Font8x19[95][19] = {\n")
        for c in range(0x20, 0x7f):
            f.write("  { %s },\n" % ", ".join("0x%02X" % x for x in g[c]))
        f.write("};\n")
    print("wrote", out)


make_font()
apply(os.path.join(EDK2, "UefiPayloadPkg", "UefiPayloadPkg.dsc"), DSC_EDITS, os.path.join(OUT, "Ps4Payload.dsc"))
apply(os.path.join(EDK2, "UefiPayloadPkg", "UefiPayloadPkg.fdf"), FDF_EDITS, os.path.join(OUT, "Ps4Payload.fdf"))
