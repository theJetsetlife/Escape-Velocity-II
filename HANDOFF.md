# Handoff — where the project stands and how to continue

## 1. What works, and how sure we are
"Hardware" = run on a PS4 Fat on 9.00 and confirmed on screen. "QEMU" = passes the harness in `qemu/`,
which imitates the handoff (memory map, LAPIC state, framebuffer descriptor, parked CPUs) but not the real
southbridge, GPU or firmware.

| Milestone | Evidence | Artifacts |
|---|---|---|
| Delivery → ring 0 → arm → ICC hook (all 8 CPUs stopped) → takeover → EDK II → UEFI Shell drawn by Ps4GopDxe | **hardware** (receiver log + on-screen Shell) | `prebuilt/hw-verified/` stage `ff86e20d`, UEFI `5c32a84d` |
| USB (Aeolia xHCI ×3 via PciIo shim), AHCI, SDHCI, UART mirror, DS4 keyboard, Yukon Ethernet | QEMU | `prebuilt/io-build/` UEFI `d7a705d2` (with stage `ff86e20d`) |
| 6 GiB RAM: GPU MC reprogrammed to a 2 GiB VRAM carve at 4 GiB (as the PS4 Linux loader does), UC MTRR, 0x180000000–0x273FFFFFF released; memory map fails closed | QEMU (`MEM8G=1`: 5.31 GB available); MC/MTRR part only testable on hardware | current tree / `prebuilt/rebuild2-current/` |
| DMA bounce buffers below 2 GiB for 31-bit southbridge masters; multi-BAR PciIo shim | QEMU | current tree |
| NTFS (read-only, OpenNtfsDxe port) | QEMU | current tree |
| `ps4log` one-command log + boot-time auto-save, 1 MiB boot-log ring | QEMU (LAN/DHCP/DNS/PXE/TFTP PASS on virtio-net) | current tree |
| Windows 10 Setup first screen through this firmware | QEMU (with the PS4 DSDT, real QEMU IOAPIC) | current tree |

**Nothing after the hardware-verified pair has run on a PS4 yet.** The next hardware run should be the
io-build (one change set), then the current tree.

## 2. Rules learned the hard way (each one cost hardware runs)
1. **Never write PCI configuration space of devices OrbisOS left running** (BARs, command register, Bus Master).
   Clearing Bus Master powered the console off; PciBusDxe enumeration killed it. That is why PciBusDxe is not in
   the image and devices get a PciIo from `Ps4SouthbridgeIoDxe` (config reads only; writes only to status and ≥0x40).
2. **Never let EDK II send INIT/SIPI to the parked APs** — only MpInitLibUp. DxeMpInitLib restarted the 7 parked
   cores inside EDK II and corrupted memory at random points.
3. **The BSP local APIC arrives dirty** (the takeover can run inside an OrbisOS interrupt handler): `Ps4ApicFixupDxe`
   clears ISR/TPR and absorbs IRR before anything relies on the timer.
4. **No M_WAITOK allocations in ring-0 arm**; every arm step is its own syscall and is flushed to the receiver.
5. **Test every change in QEMU first** (`make qemu` → `MATRIX: PASS`), then one change set per hardware run.
6. **Review every frame of a run video, especially the end** (a 2 fps sample once missed a black-out).
7. **Never modify WebKitty code** — only the payload file in the GoldHEN slot (`tools/deploy.sh` does exactly that).
8. **Never probe GoldHEN's BinLoader port 9090** (an empty connection makes it jump into an empty buffer and kills
   it); if a payload is sent with BinLoader, send it once (`tools/diag_recv.py send`). ps4debug: send once, a
   double load freezes the console.
9. The stage arms on 9.00 only. It recognises 21 more versions (5.05–14.02, community offsets) and refuses them
   until their `phys_avail` is known: docs/FIRMWARE_VERSIONS.md.
10. Shell gotcha (zsh): pass environment variables explicitly (`SMP=1 ./run.sh`), not via an unquoted `$var`.

## 3. Open problems, in priority order
1. **Hardware run of the io-build, then of the current tree.** Watch: USB enumeration on the real Aeolia xHCIs,
   DS4 input, Ethernet DHCP; for the current tree: does the console survive `configure_vram` (GPU MC reprogram)
   and does the UEFI memory map show ~6 GB (`memmap` or `ps4log`).
2. **Windows 10 needs an IOAPIC** (docs/WINDOWS10.md): with no IOAPIC in the MADT the HAL stops with 0x5C
   (C000000E). A RAM page standing in for one is not enough (0x5C, 0x110, C000000D). In QEMU the real IOAPIC
   works even with every other Sony-like MADT property. A test of the RAM IOAPIC with MADT ID 0 (instead of 8)
   is built into the current tree but its result is unknown. If a RAM page cannot satisfy the HAL, options are a
   thin SVM hypervisor emulating an IOAPIC (Jaguar has SVM; check VM_CR in `ps4log`), or an MMIO region of the
   southbridge that can act as one.
3. **Windows FADT**: Sony's FADT routes SCI 9 (no IOAPIC route), its PM timer at 0x1008 does not count, it is not
   hardware-reduced. Expect this to be the next HAL/ACPI obstacle on hardware.
4. **Windows USB boot** (the end goal: Windows from an external USB drive): the PS4's three xHCI controllers sit
   behind one PCI function with a Sony class code and Aeolia MSI routing; Windows' inbox driver will not bind.
   Likely route: an INF using `mf.sys` to split the function + MSI programming. Research stage.
5. **Other system software versions**: the stage selects per-version offsets at run time (docs/FIRMWARE_VERSIONS.md);
   each further version needs a kernel dump to verify its row and find `phys_avail`, then a hardware run.
6. Smaller: 30 s hold in the stage (`DB_HOLD_SECONDS`) can shrink; fb-console text overlaps the Shell banner;
   reset/power-off need ICC requests to syscon (currently the CPU is parked); NTFS is read-only.

## 4. Where things are
- Architecture and interfaces: `docs/ARCHITECTURE.md`. Build: `docs/BUILD.md`. Tests and QEMU modes:
  `docs/TESTING.md`. Hardware procedure, markers and reports: `docs/HARDWARE.md`.
- Original engineering notes (fact ledger with evidence ids used in code comments, roadmap, milestone, status,
  delivery chain, audits): `docs/notes/`. Measured hardware data (diag runs 2 and 3: memory map, MSRs, PCI,
  ACPI): `reference/analysis/run2`, `run3`.
- Sony ACPI tables, SDK headers, PS4 Linux driver sources, the PS4 Linux loader, WebKitty/GoldHEN sources:
  `reference/` (`reference/NOTICE.md`).
- Reading bugchecks without a debugger (QEMU): `docs/TESTING.md` §5, scripts in `tools/win10/`.
