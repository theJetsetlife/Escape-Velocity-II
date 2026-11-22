# Escape Velocity II — isolated SAMU host-side analysis laboratory

**Scope:** OrbisOS 9.00 **x86 host** SBL/SAMU driver behavior.  The lab does **not** control SAMU, build PS4/EDK II payloads, access hardware registers, decrypt firmware, or recover cryptographic keys.

## Clean-room meaning and provenance

This is a **clean-room test execution environment** using independently invented fixtures and a synthetic device callback. The host model was developed using instruction-level observations of a user-provided proprietary kernel image: it **is not** an independently designed, two-team legal clean-room reimplementation of Sony's copyrighted driver. Keep that distinction in external communications.

The original `kernel(1).bin` has SHA-256 `8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3` and may include console-specific runtime material beyond ELF `PT_LOAD` file ranges. Keep it private and do not check it into Git. No kernel, NOR, firmware images, decrypted blobs, or keys are included in the lab.

## Offline workflow (no hardware, network or proprietary input)

From the repository root, with Python 3, a C11 compiler, and Clang/libFuzzer:

```sh
make -C tools/samu-research cleanroom
```

| Target | Test inputs | Security property verified |
|---|---|---|
| `guard` | Source tree only | Reject local blobs, symlinks, embedded private-key blocks, known network/live-memory primitives |
| `test` | Invented ELF and C mock state | ELF mapping, malformed metadata, error bounds, fake device callbacks, graph decoding |
| `sanitizers` | C mock state | ASan/UBSan memory and undefined-behavior checks |
| `fuzz` | Invented ELF mutations and mock requests | Deterministic parser invariants and C libFuzzer crash detection |
| `static-analysis` | C mock implementation | Clang static analyzer diagnostics |

`make cleanroom` is intentionally independent of any kernel capture and can run in CI. Artifacts are written under `tools/samu-research/.build/` and are excluded from source guards and uploads. A passing unit or fuzz run is **not** a vulnerability assessment of real SAMU firmware.

## Local kernel evidence gate (separate from clean-room CI)

On a workstation with the authorized, **unaltered** PS4 9.00 dump:

```sh
make -C tools/samu-research verify-kernel KDUMP='/private/path/kernel(1).bin'
```

The command verifies the exact image hash, ELF64 FreeBSD x86-64 structure, program headers, entry mapping, 19 pinned instruction byte sites, and five previously cited direct rel32 call/jump edges. It prints only status; detailed metadata is retained privately in `.build/local_kernel_verification.json` and `.build/local_call_graph_verification.json`. Three graph edges remain `inferred` and cannot be promoted without further independent disassembly.

The verifier rejects symlinks, device files, oversize inputs (>128 MiB), malformed ELF entries, overlapping virtual mappings, and out-of-file mappings. It reads file bytes only; there is no live memory, DMA or hardware access.

## Optional: locked-down Docker test run

The image build may need network access **only to download the Ubuntu base and install the toolchain**. It uses the research directory alone as build context, protected by `.dockerignore`; do not change the context to the repository root.

```sh
docker build -t evii-samu-cleanroom:local \
  -f tools/samu-research/Dockerfile.cleanroom tools/samu-research

docker run --rm --network none --read-only \
  --cap-drop ALL --security-opt no-new-privileges \
  --pids-limit 128 --memory 2g --cpus 2 \
  --tmpfs /tmp:rw,nosuid,nodev,size=64m \
  --tmpfs /lab/tools/samu-research/.build:rw,exec,nosuid,nodev,uid=65532,gid=65532,size=128m \
  evii-samu-cleanroom:local
```

No PS4 volumes are mounted in the container. The image runs as UID 65532 and defaults to `make test sanitizers fuzz static-analysis` plus a source-only guard. **Docker execution has not been tested in the authoring environment**; the equivalent host-side tool targets have been exercised locally. Base-image digest pinning is recommended for a final long-lived release.

## Required review gates for any future PS4 driver

1. Keep `prebuilt/hw-verified/` and `src/directboot/` immutable during research.
2. Treat kernel offsets as evidence specific to this captured image, never as firmware-portable patch sites.
3. Document GPUVM ownership, controller initialization, synchronization, and cache visibility before any hardware communication.
4. Use separate read-only diagnostics and HAL reviews; no live MMIO writes, arbitrary physical-memory reads, protected-key extraction or firmware mutation in this lab.
5. Run repository QEMU regression separately when a **future** UEFI integration is proposed. The present lab does not modify the boot path or claim real-console testing.

## Threat model / excluded claims

The verifier handles untrusted local *files*. The C model handles hostile synthetic *request lengths* and callback behavior. The source guard is a policy check, **not** a sandbox: it cannot prevent malicious toolchain execution or replace Docker/VM isolation. Branches marked `inferred` are hypotheses; ABI values and communication semantics still require independent REA/Ghidra validation. CI test success never implies SAMU exploitation or protected-key visibility.


## Real-firmware corpus (local analysis)

This lab also supports **actual proprietary firmware binaries supplied by the console owner**. The real-corpus path is opt-in and independent from synthetic CI fuzz tests. Import the user's 9.00 `kernel(1).bin` and 32 MiB `sflash0.bin` with `make corpus-init KDUMP=... NOR=...`. The extractor materializes and verifies all four SAMU/CoreOS partitions at their absolute NOR offsets. Run `make test-real` to verify known SHA-256 hashes, segment-aware kernel instruction bytes, and the direct call-graph checks. This tests genuine supplied code/data rather than synthetic firmware. No live device access occurs.

Use `.local-corpus/` in the source checkout, or a private local ZIP containing all six real images and the manifest. The GitHub branch and synthetic CI remain code-only.
