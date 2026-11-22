# Security review: SAMU 9.00 offline host reconstruction

**2026-11-22.** Static and synthetic security analysis only; no direct access to a PS4 controller, no REA/Ghidra session, no private key material.

## Threat model

**Assets:** integrity of the known-working Escape Velocity II boot artifacts, privacy of console memory and firmware dumps, correctness of version-specific reverse-engineering evidence, reproducibility of test results, safety of developer workstations.

**Adversarial inputs:** malformed/crafted ELF headers; overlapping executable segments; spoofed instruction-byte patterns; unbounded input files; malicious local path types; invalid synthetic request lengths; buffer aliasing; changed runtime-address assumptions; source-tree binary/secret contamination; mistaken decompiler confidence.

**Out of scope:** active fault/glitch testing, hardware MMIO/DMA, firmware decryption, key exfiltration, code execution inside SAMU, kernel patching, or network-facing fuzzing.

## Specific findings and mitigations

| ID | Class | Finding / mitigation | Status |
|---|---|---|---|
| SAMU-01 | Evidence integrity | Earlier verifier incorrectly parsed zero program headers yet reported 19/19: fixed in PR #3 by ELF segment mapping | Fixed, regression-tested |
| SAMU-02 | Input resource exhaustion | Prior verifier accepted up to 1 GiB and read entire files; now regular files only, no symlinks/devices, bounded to 128 MiB | Fixed, tested |
| SAMU-03 | Address confusion | Reject virtual address wrapping, zero-length requests, non-RX segments, ambiguous overlaps and mappings into BSS | Fixed, tested |
| SAMU-04 | ELF metadata validation | Reject overlapping/invalid program-header table and nonsensical segment alignment | Fixed, tested |
| SAMU-05 | Inaccurate call graph | Five previously claimed direct call/jump edges now checked by rel32 opcode and computed target; three speculative edges stay `inferred` | 5 validated; 3 unresolved |
| SAMU-06 | Mock API aliasing | Caller buffer overlapping mock device storage previously had undefined copy semantics; model now rejects aliasing | Fixed, ASan/UBSan tested |
| SAMU-07 | Untrusted corpus contamination | Research folder source guard detects binaries, key blocks, symlinks, and selected network/live-memory primitive patterns | Guarded, not a sandbox |
| SAMU-08 | Fuzz/test coverage | Deterministic synthetic ELF mutations and libFuzzer synthetic message cases; no real kernel needed in CI | Automated offline checks |
| SAMU-09 | Hardware interface mapping | GPU BAR ownership, cache visibility, interrupt ordering, index-port access and controller acceptance are not independently validated | Open; excluded from hardware build |
| SAMU-10 | REA decompiler provenance | Instruction-level evidence exists; an REA/Ghidra decompilation has not been executed in this environment | Open; workflow documented |

## Static host-side driver observations

These findings are **facts about the particular captured firmware 9.00 host kernel**, not evidence of a vulnerability in SAMU itself:

- The message sender validates 24 through 4096 bytes, uses a 1 MiB host allocation with a message offset of `0x1000`, and reads a completion status at `+0x1004`.
- A higher-level SBL service accepts lengths up to 168 bytes.
- Indexed GPU register helpers operate through relative selector/data offsets `0x22000`/`0x22004`; these are *not* safe standalone physical MMIO addresses.
- A dispatch candidate compares against 13 callback entries; the actual callback registration and authorization rules remain incomplete.
- Five direct edges in `call_graph.json` can be validated from original machine-code relative branch displacements. Three edges remain inferences.

**Interpretation:** the input bounds and host-side transport structure support an architectural reconstruction, but do not demonstrate bypass of SAMU authorization, firmware integrity verification, encrypted NOR, or protected keyrings.

## Evidence levels

- **Verified instruction site:** exact image hash, ELF mapping, instruction bytes matched; *not* necessarily a complete function-level semantic proof.
- **Verified direct edge:** opcode plus relative displacement matches the declared target in an executable segment.
- **Strong candidate:** assembly/context suggests a role, but complete control flow and error handling are not yet established.
- **Inferred edge:** based on behavioral similarity or dataflow context; must not be treated as verified.

## Reproducibility

```sh
make -C tools/samu-research cleanroom
make -C tools/samu-research verify-kernel KDUMP='/private/kernel(1).bin'
```

Use `docs/SAMU_900_REA_WORKFLOW.md` to promote or revise any candidate after independent REA/Ghidra work. Attach provider/software versions and relevant instruction evidence to a subsequent review. Do not publish proprietary dump bytes or console-specific memory.
