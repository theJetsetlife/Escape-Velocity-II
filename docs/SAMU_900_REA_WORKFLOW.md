# REA/Ghidra workflow: verify PS4 9.00 SAMU host-driver reconstruction

**Status (2026-11-22):** A complete REA MCP analysis has **not** been run in the current workspace. REA is not installed here, the npm registry was unreachable during this audit, and no Ghidra backend is present. The fixes in this branch were verified using GNU binutils + offline tests. This document makes a repeatable **local REA verification** the next analysis gate, without inventing tool output.

Official project: https://github.com/morluto/rea . Use only approved installation channels; review its setup changes before proceeding.

## Local workstation prerequisites

- Install a supported Node.js version and npm (consult REA's current `package.json` engine requirement).
- Install Ghidra and Java; point REA at your existing, trusted installation.
- Use a local PS4 firmware 9.00 kernel dump you are authorized to inspect. Keep the **original 42 MiB dump on the local workstation**: it may contain runtime or console-specific data outside ELF load segments. Never attach or push it to a public repository.
- Pin a release of `rea-agents` appropriate to your environment instead of silently adopting future updates.

Set up the Codex MCP integration from a trusted directory:

```sh
# Choose and audit a fixed release; do NOT silently run @latest.
REA_VERSION='<audited-version>'
npx --yes "rea-agents@${REA_VERSION}" setup --client codex
npx --yes "rea-agents@${REA_VERSION}" doctor --client codex --json
```

Review setup changes, restart Codex, and confirm Ghidra is the configured analysis backend. If either operation fails, do not claim a successful REA verification.

## Suggested task for the REA-enabled agent

> Use REA with a local Ghidra provider to analyze the executable ELF PT_LOAD portions of `/absolute/path/to/kernel(1).bin` from firmware 9.00. Verify SHA-256 `8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3`. Analyze the **host-side** routines at `0xffffffff999e42b0` (init candidate), `0xffffffff999e4ee0` (message sender), `0xffffffff999e4a20` (submission), `0xffffffff999e45b0` (dispatch), `0xffffffff999f9060` (service candidate), `0xffffffff99884950` and `0xffffffff998849e0` (indexed access). Recover assembly, pseudocode, call targets, data references, and potential structure fields. Confirm each inference against machine instructions and label unconfirmed names as provisional. Export a call graph and findings with evidence by virtual address. Compare the result with `docs/SAMU_900_HOST_DRIVER.md` and `tools/samu-research/call_graph.json`, proposing documentation-only corrections. Avoid hardware writes, memory dumping beyond declared ELF segments, and attempts to obtain protected cryptographic keys.

## Required REA validation report

Each claim needs: firmware dump SHA-256; virtual address; instruction bytes or actual pseudocode; validated call/xref target; confidence; discrepancy versus existing notes. Include exact tool/provider versions and the actual session output references. No synthetic session IDs or pseudocode may be represented as tool output.

## Independent offline verification gate

```sh
make -C tools/samu-research cleanroom
make -C tools/samu-research verify-kernel KDUMP='/absolute/path/to/kernel(1).bin'
```

The verifier checks: exact 9.00 dump SHA-256; 64-bit FreeBSD/x86-64 ELF identity; declared program headers; entry point; executable RX segment mapping; and 19 pinned instruction sequences. A 19/19 result alone is **not** evidence of a full decompilation or of hardware-level security behavior.
