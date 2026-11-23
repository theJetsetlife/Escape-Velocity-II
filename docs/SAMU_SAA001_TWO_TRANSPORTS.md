# SAMU on SAA-001 — separate host and SNVS routes

**Date:** 2026-11-22
**Scope:** offline research update based on the user's 9.00 x86 kernel and public motherboard research.
**Status:** host code trace observed; no SAMU internal firmware trace or live EDK II transaction.

## A. Ordinary host security service route — local binary evidence

```text
Orbis x86-64 SBL service (max 168 bytes at checked API)
  -> SBL send wrapper
  -> SBL sender (24..4096 bytes at checked internal API)
  -> 1 MiB shared allocation (16 KiB alignment observed)
  -> GPU/APU host-indexed register interface
  -> SAMU (internal handling NOT captured)
```

The 9.00 kernel image SHA-256 is `8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3`.
Exact kernel virtual addresses and bytes are in `tools/samu-research/evidence.json`; direct edges and three unresolved inferences are in `call_graph.json`. The index/data offsets `0x22000`/`0x22004` are **relative to the OS GPU register abstraction**, not validated physical addresses in EDK II.

PCI enumeration identified Liverpool GPU `1002:9920`, but does not establish whether its GPUVM mappings or interrupt state survive native takeover.

## B. SNVS route — external published evidence

```text
SAMU-protected nonvolatile data
  <-> APU / auxiliary communication
  <-> Aeolia ICC / ChipComm handling
  <-> Syscon secure nonvolatile storage (SNVS)
```

fail0verflow described SAA-001 Aeolia-to-Syscon ICC links and Syscon SNVS cryptographic protection involving SAMU. **This is external published research**, not a decoded bus capture from our SFlash or UEFI logs. These observations do **not** establish that ordinary SBL requests flow through EAP/EMC/Syscon.

Primary references:
- https://fail0verflow.com/blog/2018/ps4-aeolia/
- https://fail0verflow.com/blog/2018/ps4-syscon/

Board-level distinction: SAMU is internal to AMD Liverpool's APU. Aeolia (EAP/EMC) and Syscon are external devices. The public PCB research does not reveal internal APU net routing.

## C. Consequences for firmware-perimeter research

EAP KBL, EAP Kernel, EMC IPL, and Syscon images are sensible targets for **offline compatibility and boot-state analysis**, but modifying them is not a verified method for intercepting routine SAMU SBL requests, replacing SAMU secure firmware, or bypassing cryptographic policy.

No files under `src/directboot/`, `Ps4Pkg/`, or `prebuilt/hw-verified/` need to change for this evidence update. Preserve the hardware-working build unchanged. There must be no blind MMIO reads/writes, no DMA mappings based on guesses, and no proprietary firmware or keys committed to the repository.

## D. Automated offline evidence gate

```bash
make -C tools/samu-research topology
make -C tools/samu-research cleanroom
```

The first command cross-checks both routes against pinned evidence metadata and verified call-graph edges. The tests reject accidental mergers of the SBL and ICC paths. Passing this check does not prove a hardware interaction.
