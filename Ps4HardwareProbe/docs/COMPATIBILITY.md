# Ps4HardwareProbe compatibility matrix

States: **Verified on physical hardware** · **Partially verified** · **Experimental** · **Unsupported / not yet tested**.

**The Ps4HardwareProbe binary has not run on any PS4.** "Partially verified" means the same reads were made
on a real console by this repository's earlier read-only payload (`src/diag`, runs 2 and 3 on a PS4 Fat on
9.00, `reference/analysis/run2`, `run3`), not by this probe. Everything else rests on public sources (PS4
Linux, OpenGoldHEN, PS4 Payload SDK) and on the mock-hardware host tests, which prove the logic, gates and
backend selection, not hardware behaviour.

## Console families (identified by GPU device id, never by marketing name)

| Family | GPU id (source) | Southbridge pairing in sources | Probe state |
|---|---|---|---|
| CUH-10xx / 11xx | `0x9920` (PS4 Linux `ps4_bridge.c` CUH_11XX; 10xx assumed to share it) | Aeolia (sdhci note: CUH-1003; this project's Fat) | Partially verified |
| CUH-12xx | `0x9922` (`ps4_bridge.c` CUH_12XX) | Belize (sdhci note: CUH-1215/1216) | Unsupported / not yet tested |
| Slim CUH-20xx/21xx/22xx | `0x9923` (`ps4_bridge.c` CUH_2XXX) | none in available sources: reported as observed | Unsupported / not yet tested |
| Pro CUH-70xx/71xx/72xx | `0x9924` (`ps4_bridge.c` CUH_7XXX) | none in available sources: reported as observed | Unsupported / not yet tested |
| Any other GPU id / mixed or unknown Sony functions | - | - | limited discovery (no southbridge or GPU register reads) |

A family whose observed southbridge is not the documented pairing is downgraded to Experimental in the report.
The report names the **selected compatibility profile** (board + southbridge + firmware) and its state, the
weakest of the three.

## Southbridge backends (`southbridge/`)

| Chip | PCI ids (vendor 0x104d) | Register windows | Read level | State |
|---|---|---|---|---|
| Aeolia | 908f, 909e-90a4 | identity BAR2+0x1104/0x1108/0x110c, RTC BAR2+0x130 | 1 (read at every boot by PS4 Linux sb-aeolia.c / sb-rtc.c) | Partially verified |
| Aeolia | | glue BAR4+0x1c8000 (BAR windows), +0x1c8400 (MSI) | 1 (read on HW by src/diag run 2) | Partially verified |
| Belize | 90c8-90cf | identity + RTC as Aeolia (PS4 Linux binds Belize to the Aeolia driver) | 1 | Experimental |
| Belize | | glue windows as Aeolia | 2 | Experimental |
| Baikal | 90d7-90de | identity BAR4+0xc020/0xc024, rev 0x4084; RTC BAR4+0x50cc (read at every boot by sb-baikal.c / sb-rtc.c) | 1 | Experimental |
| Baikal | | HPET BAR4+0x109000 | 2 | Experimental |
| unknown | - | none | - | limited discovery |

Never read on any chip: ICC registers and SPM (reads are part of the syscon protocol), anything requiring a write.

## Firmware profiles (`firmware/profiles/fwtable.h`, vendored, 22 rows)

| Versions | Source | State | Default behaviour (strict 0) |
|---|---|---|---|
| 9.00 | kernel dump + hardware runs | Verified on physical hardware (offsets) / probe not run | used after the same runtime validation |
| 5.05 6.72 7.00 7.50 8.00 8.50 9.03 9.60 10.00 10.50 11.00 11.02 11.50 12.00 12.50 13.00 13.02 13.50 13.52 14.00 14.02 | PS4 Linux loader, cross-checked vs OpenGoldHEN | Experimental | used when the live kernel matches every code-site signature of the row and the direct map proves itself (runtime validation); `--strict 1` refuses them |
| 6.71, 8.01, 8.03, 8.52, 9.04, 9.50, 9.51, 10.01, 10.70, 10.71, 11.52, 12.02, 12.52 | share a row's kernel layout (whole OpenGoldHEN import table equal, docs/FIRMWARE_VERSIONS.md) | Experimental | the shared row, same runtime validation |
| anything else (e.g. < 5.05, 7.02, 7.51, 7.55, > 14.02), or version unreadable | no published offsets | Unsupported | no kernel offsets: CPU-level diagnostics only, reported as UNSUPPORTED |

35 versions have a DMAP profile. Every one is checked against the running kernel before use; the table is a
claim, the live kernel decides. 9.00 is the only row verified on hardware.

### Identity profiles (`firmware/profiles/fwident.h`, generated from OpenGoldHEN by `make fwident`)

A second, independent source for 33 versions: 5.05 6.71 6.72 7.02 7.50 7.51 7.55 8.00 8.01 8.03 8.50 8.52 9.00
9.03 9.04 9.50 9.51 9.60 10.00 10.01 10.50 10.70 10.71 11.00 11.02 11.50 11.52 12.00 12.02 12.50 12.52 13.00
(OpenGoldHEN tables) and 14.00 (table extracted from an unofficial GoldHEN 14.00 BD-J build; it matches the
independent 14.00 offset row on kmem_alloc, kern_reboot and kernel_map; provenance in
`firmware/profiles/sources/README.md`).
Each checks 8 function prologues, the `kexec_rop` gadget and the `kernel_map` pointer on the live kernel
(all checked against the retail 9.00 dump by the generator; Xfast_syscall = LSTAR - 0x1c0 on all 32).

| Situation | Firmware state | Effect |
|---|---|---|
| identity + DMAP profile both match | validated by two sources (9.00: hardware-verified) | every diagnostic |
| DMAP profile only (7.00, 13.02, 13.50, 13.52, 14.02) | validated at runtime only | every diagnostic |
| identity only (7.02, 7.51, 7.55, 10.70) | identity confirmed, no DMAP profile | CPU/MSR/topology/TSC; memory-dependent ones UNSUPPORTED |
| a profile exists but the live kernel disagrees, or the two sources disagree | unsupported | no offsets used, FAILED with the reason |

## Loaders (`adapters/`)

| Loader | Contract | State |
|---|---|---|
| GoldHEN BinLoader (port 9090), 29 versions 5.05-13.00 + 14.00 (unofficial build) | raw syscalls; kexec = syscall 11 (`sysent[11] = kexec_rop`, OpenGoldHEN patches.c; `jmp [rsi]` on 9.00) | Experimental (default build) |
| WebKitty psfree-lapse, 7.00-9.60 (17 versions, port 9020) | raw syscalls; kexec = syscall 11 (kpatch writes `sysent[11] = jmp_rsi`) | Partially verified on 9.00 (src/diag), Experimental elsewhere |
| WebKitty relapse, 11.00-13.52 (11 versions) | raw syscalls; kexec = syscall 661 (jb.js overwrites `sysent[661]` with `k_jmp_rsi`) | Experimental |
| WebKitty on any other version | no kexec known | no ring0 (D_KEXEC SKIPPED) |
| anything else | undeclared | payload returns without doing anything |

The kexec syscall number is chosen at run time from the loader and the libc.sprx version; calling a
syscall the loader did not install would kill the host process, so an unknown pairing gets no ring0.

## What each diagnostic needs

| Diagnostic | Needs | Without it |
|---|---|---|
| CPU id, topology, CR0/3/4, EFER | ring0 (kexec) | not run (no ring0) |
| MSRs, MTRRs, ECAM base | family 16h | SKIPPED (safety) |
| PCI, platform, southbridge, GPU, USB, SATA, SDHCI, network, ACPI, DMAP map, LAPIC, HPET | accepted firmware profile + proven direct map | UNSUPPORTED |
| southbridge / GPU registers | backend window level <= run level | SKIPPED (safety) |
| LAPIC, HPET, standard controller capability registers | level 2 | SKIPPED (safety) |
