# PS4 9.00 SAMU host-driver reconstruction (offline-only)

This directory intentionally lives under `tools/`, **not** `Ps4Pkg/` or `src/directboot/`. It does not build an EFI module and has no direct GPU/SAMU I/O. It models **Sony's x86 host-side SBL message path**, not SAMU firmware, private RAM, or key access.

## Test

```sh
make -C tools/samu-research test
make -C tools/samu-research verify-kernel KDUMP='/path/to/kernel(1).bin'
```

Expected outputs: 9 model assertions pass, 19 pinned assembly checks pass, and the kernel SHA-256 matches the recorded firmware 9.00 capture. Running without the exact kernel file will fail; it never fetches kernel data.

## Artifacts

- `sbl_host_model.c` — clean-room offline model with synthetic completion callback only
- `verify_kernel_900.py` — compares pinned instructions against original kernel binary
- `evidence.json` — recorded inspection results from user-supplied binary
- `call_graph.json` — verified edges and inferred roles, with explicit confidence

**Do not link this model into native firmware.** Full GPUVM, cache coherency, interrupts, error recovery, and SAMU-specific authorization rules are not reconstructed.

### Verifier integrity and REA follow-up

The corrected verifier reads **all six** ELF program headers, validates the
FreeBSD ELF64 x86-64 identity and exact dump hash, and permits pinned checks
only in executable, file-backed PT_LOAD segments. The earlier script parsed
zero segments due to incorrect ELF tuple indexes; its apparent 19/19 result
must not be used as structural verification. `make test` includes nine ELF
negative/positive regression tests and thirteen host-model assertions.

`docs/SAMU_900_REA_WORKFLOW.md` describes how to run an independent local
REA/Ghidra decompilation with Codex. REA was **not run** in the environment
that produced this overlay.
