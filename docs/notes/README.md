# Original engineering notes (historical)

These are the working notes written during development, kept unedited because code comments cite them (fact ids
such as "LEDGER A1", hardware run times, diag run numbers). They refer to the original working folders:

| Notes say | In this tree |
|---|---|
| `rebuild/` (milestone tree), `rebuild2/` (6 GiB / NTFS / ps4log / Windows tree) | the repository root (this tree = rebuild2) |
| `rebuild/edk2`, `rebuild2/edk2` | `edk2/` from `scripts/setup-edk2.sh` |
| `rebuild/webkitty-docker/` | `server/` |
| `10.0.0.71` (PC), `10.0.0.12` (PS4) | your `server/.env` |
| `ref/`, `analysis/` | `reference/`, `reference/analysis/` |
| `backups/snapshot-*` | not included (binaries of note are in `prebuilt/`) |

| File | Content |
|---|---|
| `LEDGER.md` | verified facts with evidence (GREEN/RED), the basis for every hardware assumption |
| `ROADMAP.md`, `NEXT_STEPS.md` | phase plan toward a release-grade UEFI and the order it was worked in |
| `CHAIN.md`, `REFERENCE_CHAIN.md` | the delivery chain step by step, and the PS4 Linux loader chain it mirrors |
| `ARCHITECTURE.md`, `SOURCES.md` | early layer map; sources and hashes of everything referenced |
| `MILESTONE_2026-11-20_UEFI_SHELL.md` | the first successful hardware run: artifacts and each blocker → cause → fix |
| `STATUS_2026-11-20.md`, `WIN10_PROGRESS_2026-11-20.md` | status snapshots |
| `contracts/PS4_MEMORY_GLUE_CONTRACT_v1.md` | memory / southbridge glue contract for the handoff |
