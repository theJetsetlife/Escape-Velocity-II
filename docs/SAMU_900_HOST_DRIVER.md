# PS4 9.00 host SAMU driver — instruction-backed reconstruction

> **Scope:** Reverse engineering the **Orbis x86-64 host-side SBL driver** from a PS4 kernel dump. This is not Sony's source and is not a claim of SAMU firmware access.

## Provenance

- User-supplied input `kernel(1).bin`, 44,040,192 bytes (`0x2a00000`).
- SHA-256 `8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3`.
- ELF64 x86-64, first program segment VA `0xffffffff993c8000`.
- Source-build paths suggest `J02541110`; an embedded release string identifies `release_09.000`. Hybrid/debug provenance is not independently established.
- Runtime VAs below belong **only** to this capture, are not stable symbols, and must not be used as kernel patch offsets.

## Reconstructed path

```text
SBL service candidate [0xffffffff999f9060]
  └── direct call → no-copyback wrapper [0xffffffff999e4ed0]
          └── direct tail jump → sender [0xffffffff999e4ee0]
                 ├── bounds: 24 <= request bytes <= 4096
                 ├── host buffer allocation base +0x1000
                 ├── clear 8 bytes at +0x1010
                 ├── submit via [0xffffffff999e4a20]
                 └── read signed status at base +0x1004

SBL initialization candidate [0xffffffff999e42b0]
  ├── allocate 0x100000 bytes; 0x4000 alignment check
  ├── candidate GPUVM mapping via [0xffffffff999e3480]
  └── same submission routine [0xffffffff999e4a20]

SBL completion dispatcher candidate [0xffffffff999e45b0]
  └── command IDs 0..12, callback registry and fallback

GPU indexed-register access [0xffffffff99884950, 0xffffffff998849e0]
  └── index/data offsets +0x22000/+0x22004 in the GPU's register abstraction
```

The top-level service function rejects sizes ≥169; its actual minimum and all service message types remain to be established. The driver sender's lower bound of 24 and upper bound of 4096 are validated independently from assembly in this exact 9.00 dump.

## Evidence snapshots

| Observation | Instruction location | Evidence level |
|---|---|---|
| Driver request maximum 4096 | `0xffffffff999e4eed` | Verified machine bytes |
| Driver request minimum 24 | `0xffffffff999e4ef5` | Verified machine bytes + branch interpretation |
| Service request rejects sizes ≥169 | `0xffffffff999f9074` | Verified machine bytes + branch interpretation |
| Allocate 1 MiB | `0xffffffff999e4349` | Verified machine bytes; allocator semantics inferred |
| Check 16KiB alignment | `0xffffffff999e4378` | Verified machine bytes |
| SAMU indexed selector offset `0x22000` | `0xffffffff99884979` | Verified machine bytes |
| SAMU indexed data offset `0x22004` | `0xffffffff99884997` | Verified machine bytes |
| 13-entry callback dispatch | `0xffffffff999e45c1` | Verified machine bytes; handler structure inferred |
| Service to sender call | `0xffffffff999f9197` | Verified relative call |

The core submission routine writes indexed register selectors **0x34, 0x33, 0x32** and reads **0x4a**; these observations do **not** establish safe MMIO addresses nor a stable GPU register mapping in native EDK II. A direct host-side register port does not prove that SAMU accepts an arbitrary request.

## What remains unresolved

1. Exact secure service command ABI and authorization decisions.
2. GPUVM address translation, page-table creation, DMA visibility, and cache coherency.
3. 13 dispatch callback registrations and completion state machine.
4. Concurrency, interrupt signaling, timeouts, and error paths.
5. Which native UEFI HAL resources are initialized sufficiently to support non-destructive reads.
6. Which registers are diagnostic versus destructive; no writes should be attempted from an unvalidated driver.

## Boundaries

The earlier `Kernel.c` source comes from build `J00739801` and should not be used to assign names or behavior to the present `J02541110` image without independent evidence. The dumped ELF has ~23.96 MB of trailing data outside declared file-backed segments; this overlay deliberately excludes it and the raw dump.

This work contains an **offline behavioral model** with a synthetic callback only. It is neither complete Sony source code nor a live SAMU driver.

## Verifier correction (2026-11-22)

The first overlay's `verify_kernel_900.py` incorrectly used negative tuple indexes
for ELF program-header fields: `[-4]` selected `e_phnum` (6) instead of
`e_phentsize` (56), and `[-3]` selected `e_shentsize` (0) instead of
`e_phnum` (6). That version's `19/19` count was from a flat
`virtual_address - 0xffffffff993c8000` lookup with **zero parsed segment records**.

The corrected verifier parses all six actual program headers and resolves
pinned instruction addresses through the executable PT_LOAD mapping. The
original 19 instruction-byte checks still pass for the supplied dump; the
stricter checks now reject missing executable segments and malformed ELF
metadata. See `test_verifier.py` for negative regression cases. The corrected
result is evidence of **these specific instruction sequences only**, not a
full decompilation or live SAMU communication.

For a subsequent Ghidra-assisted reanalysis using **REA (Reverse Engineer
Anything)**, see `docs/SAMU_900_REA_WORKFLOW.md`. No completed REA session is
claimed in this version.
