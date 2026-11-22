# SAMU 9.00 host-only clean-room analysis lab

This is an **offline, synthetic-input test framework** for instruction-backed reconstruction of the **x86 host-side SBL/SAMU driver**. It is neither Sony source code nor executable SAMU firmware. Nothing in this module runs on the PS4, interfaces with hardware, obtains protected keys, or alters the working UEFI chain.

## All-synthetic tests

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

**Never commit the kernel dump** or any console-specific memory/firmware to the repository. Private JSON metadata from local verification stays under `.build/`.

## Files

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
