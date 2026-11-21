## STATUS 2026-11-20 -> see STATUS_2026-11-20.md (snapshot backups/snapshot-20261120-0615, copy in ~/ps4-snapshots/)

## STATUS 2026-11-19 — directboot ready for its first console run
- `src/directboot/directboot.bin` (sha 7aefeb48…) in the GoldHEN slot; receiver serves `out/upl-ps4.elf`
  (sha 9e9270fe…, built from upstream edk2-stable202608 + Ps4Pkg by `Ps4Pkg/build.sh`).
- `upl_load` host-tested on the real payload (42 relocs, FVs, HOBs) — `tools/upl_hosttest.c`.
- Fully automatic after jailbreak (stage restarts OrbisOS itself). Progress squares: `src/directboot/README.md`.
- Next: read the run result (receiver log + which squares appeared + whether EDK II drew), then iterate:
  framebuffer DEBUG console for EDK II, Aeolia xHCI/AHCI/SDHCI bound by known MMIO BAR (polling), USB keyboard.

# Next steps toward the ultimate goal (release-ready native PS4 UEFI)

Position: roadmap phase 00 largely done (LEDGER.md, SOURCES.md, hashes). Phase 02 draft started
(contracts/PS4_MEMORY_GLUE_CONTRACT_v1.md). Nothing from the clean-room rebuild has run on hardware yet.

## Immediate: read-only diagnostic payload (phases 01 + 02 together)
**Ran on hardware (2026-11-19): run 1 proved phase 01 (delivery → userland → ring0), run 2 completed
all 8 read-only phases** — see `analysis/run2/FINDINGS.md`. Run 3 (diag v3) delivered the full DMAP map and framebuffer read-back — `analysis/run3/FINDINGS.md`. Phase 02 read-only work is essentially complete. Earlier plan text:
full DMAP PD dump (memory map), MTRR helper honouring SYSCFG Tom2ForceMemTypeWB, read-back of pixels at
the inferred fb_phys 0x6e800000. First write test (separate, needs approval): one on-screen marker at fb_phys.
Previously: **Built:** `src/diag/` → `diag.bin` (staged as WebKitty
`includes/payloads/HEN/directboot-diag.bin`, GoldHEN button). PC side: `tools/diag_recv.py listen`.
See `src/diag/README.md` for the run steps, phase table and failure codes.
After the first run: read `reports/diag-*.txt`, then turn RED rows H2b, H7, H9, H12 (and E1/E2 where covered) GREEN or
correct them, and fill contract section 7.
- Small payload, delivered via WebKitty, uses the syscall 11 path to read ring-0 state. Writes nothing to hardware.
- Reports (contract section 7):
  - OrbisOS-side glue windows (AEOLIA_REG_BAR_MASK/ADDR per function)
  - PCI config BARs of 00:14.0-14.7 and 00:01.0
  - OrbisOS physical memory map at takeover
  - display scanout address (fb_phys)
  - ACPI RSDP + original Sony tables
  - IOMMU control/status, Aeolia MSI enable/status (0x1c8408 / 0x1c843c / 0x1c844c question)
- Also proves delivery -> execution -> ring-0 on real hardware (phase 01 exit criterion).
- Output: OrbisOS on-screen notification + report file or socket to the PC. No UART exists; no SSH.
  Do not touch GoldHEN BinLoader 9090.

## Then, in order (each gated on the previous one passing on hardware)
1. Close the RED contract items from the report; freeze contract v1; choose firmware-image + handoff RAM from measured facts.
2. Phase 03 staging: allocate that RAM under OrbisOS, place the EDK II payload + a versioned handoff block, prove it survives intact.
3. Phase 04 takeover: our own ring-0 code via syscall 11 — install the ICC shutdown hook, park the 7 APs,
   build our page tables, with an on-screen marker at every step before jumping.
4. Phase 05 quiesce: southbridge interrupts, IOMMU and GPU DMA shut off in a deliberate order, each verified on hardware.
5. First native EDK II entry through src/upl_load.c -> "Bootable" (Shell on screen).
6. Phases 07-09 inside EDK II: GOP/console, USB keyboard, PCI, USB storage + FAT -> "Functional".
7. Phases 10-11: test matrix, 20-cycle cold starts, reproducible builds -> "Release-ready".

## In parallel on the workstation (allowed by the roadmap's dependency rule)
- Build the EDK II Universal Payload from edk2 source ourselves.
- Exercise src/upl_load.c in QEMU with a fake memory map, so the EDK II side is ready when takeover works.

## Main risk to settle early: visibility
Every hardware stage needs a marker the operator can see. Under OrbisOS the framebuffer address is
not yet known — another reason the diagnostic payload comes first.
