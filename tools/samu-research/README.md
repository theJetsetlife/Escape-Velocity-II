# SAMU 9.00 host-only clean-room analysis lab

This is an **offline host-side verification and real-binary corpus framework** for instruction-backed reconstruction of the **x86 host-side SBL/SAMU driver**. It is neither Sony source code nor executable SAMU firmware. Nothing in this module runs on the PS4, interfaces with hardware, obtains protected keys, or alters the working UEFI chain.

## Real PS4 9.00 firmware tests (actual supplied binaries)

```sh
make -C tools/samu-research corpus-init \
  KDUMP="/path/to/kernel(1).bin" NOR=/path/to/sflash0.bin
make -C tools/samu-research test-real
```

This creates `.local-corpus/` with the actual 9.00 kernel, full NOR, both SAMU IPLs, and both CoreOS images. The four extracted firmware files are carved from the NOR and checked byte-for-byte against their offsets. SHA-256 hashes are pinned to the six supplied 9.00 samples. The actual ELF, SBL instructions, and five direct call-graph edges are rechecked. `manifest.json` records per-input identity. The test files remain available for separate static analysis and REA/Ghidra.

`.local-corpus/` is ignored by Git and excluded from the source guard. Actual binary files may be packaged into a **separate local archive** for testing; this source-only GitHub branch does not contain them.

## Synthetic regression and fuzz tests

```sh
make -C tools/samu-research cleanroom
```

Requires Python 3, a C compiler and Clang/libFuzzer. Invokes a source provenance guard, Python tests, C model assertions, ASan/UBSan, a 5,000-case deterministic Python mutator, a 3,000-execution libFuzzer smoke run, and Clang static analysis. No network or proprietary kernel is needed. Outputs are confined to `.build/`.

Optional isolated Docker instructions are in `docs/SAMU_900_CLEANROOM_LAB.md`. Docker image construction installs tools, but its test run uses no network and has no PS4 device mounts.

## Authorized local kernel verification

```sh
make -C tools/samu-research verify-kernel KDUMP='/private/path/kernel(1).bin'
```

Requires the exact **44,040,192-byte** image with SHA-256 `8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3`. Verifies 6 ELF program headers, 19 pinned instruction sites, and 5 cited rel32 call/jump graph edges; the remaining three edges stay inferred. The verifier is bound to this image, not a general PS4 kernel decoder.

The original kernel and firmware samples are now first-class **local test inputs**. Keep the corpus separate from the public source repository, while retaining it in the local testing archive. Reports remain local under `.build/` or `.local-corpus/`.

## SAA-001 transport separation (new)

`transport_topology.json` and `verify_topology.py` keep the **host x86 SBL → GPU/APU interface → SAMU** route separate from the published **SAMU-protected SNVS ↔ Aeolia ICC ↔ Syscon** route. Run `make -C tools/samu-research topology`; its metadata checks are included in the Python regression suite. This is offline analysis, not a live transport or firmware patch. See `docs/SAMU_SAA001_TWO_TRANSPORTS.md`.

## Files

- `real_corpus.py`: import and verify the actual six binary captures and their NOR offsets
- `test_real_corpus.py`: negative tests for corpus ingestion
- `sbl_host_model.c`: stand-alone mock message path and boundary checks
- `synthetic_elf.py`, `fuzz_elf.py`: invented corpus and bounded deterministic mutation
- `fuzz_sbl_host.c`: libFuzzer target with fixed synthetic backing buffers
- `verify_kernel_900.py`, `verify_call_graph.py`: offline instruction / direct-edge evidence gates
- `test_*.py`: synthetic unit and negative-security tests
- `cleanroom_guard.py`: accidental-blob/secret and prohibited-primitive policy checks, **not a sandbox**
- `call_graph.json`: claims with confidence labels and unresolved edges
- `evidence.json`: source-traceable, non-proprietary verification metadata
- `Dockerfile.cleanroom`, `.dockerignore`: restricted host-only test container configuration

See `docs/SAMU_900_CLEANROOM_LAB.md`, `docs/SAMU_900_SECURITY_AUDIT.md`, and `docs/SAMU_900_REA_WORKFLOW.md`. An independent REA/Ghidra decompilation is still required before claiming complete function-level recovery.
