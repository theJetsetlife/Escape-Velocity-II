# Escape Velocity II: safely integrating SAMU research

## Repository structure

The public `main` README (observed 2026-11-22) defines `src/directboot/` for native Orbis-to-EDK II takeover, `Ps4Pkg/` for UEFI code, `src/diag/` and `src/k0probe/` for earlier 9.00 diagnostics, `tools/` for host utilities, `docs/` for engineering notes, and `prebuilt/hw-verified/` for known hardware-proven artifacts.

**This overlay intentionally adds only `tools/samu-research/` and `docs/` files.** It does not change the stage, HAL, firmware-version table, UEFI DSC/FDF, or prebuilt binaries.

## Installation

From your workstation checkout:

```sh
unzip EVII_SAMU_900_Research_Overlay.zip -d /path/to/Escape-Velocity-II
cd /path/to/Escape-Velocity-II
make -C tools/samu-research test
make -C tools/samu-research verify-kernel KDUMP='/path/to/kernel(1).bin'
```

A clean offline test is a prerequisite before reviewing any UEFI adaptation. The 9.00 dump is **not** included and must remain local.

## Interfaces to investigate for a future *read-only* EFI diagnostic

- `EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL` for PCI configuration discovery (already successful in prior probe).
- Dedicated Ps4Pkg HAL methods for validated GPU register base and BAR ownership; **never** map guessed GPU MMIO addresses.
- Verified read-only GPU/SAMU status only, avoiding submission/start/interrupt-acknowledge writes until a legitimate transport is fully specified and separately approved.
- Buffered USB evidence logging with bounded length and no arbitrary physical memory capture.

Before enabling hardware I/O, cross-check the repository's `HANDOFF.md`, driver DSC/FDF wiring, QEMU regression matrix, and known-working `prebuilt/hw-verified/` hashes. The public README says that the directboot+UEFI Shell chain works on a 9.00 Fat console, while many later I/O/HAL features are *QEMU-tested only*.

## Limitations of this integration

The public GitHub repository README and HANDOFF were inspected through a GitHub connection; no complete local checkout was built. Therefore this is a **copy-in offline overlay**, not a source-merged PR or demonstration of a successful EDK II compilation. Repo-specific HAL interfaces and device initialization require review against a local checkout before writing EFI code.

## REA follow-up

Use the read-only REA/Ghidra procedure in `docs/SAMU_900_REA_WORKFLOW.md` to revisit candidates before any EFI adaptation. The authoring environment could not run REA itself.
