# PS4 Native Memory, Glue and Staging Contract — v1 (evidence draft)

Roadmap work package 1 (Ps4MemoryMap + Ps4AeoliaGlue). Every row is tagged:
**P** = primary evidence, **I** = inference from primary evidence, **U** = unknown.

Primary artifacts:
- SRC = linux-ps4-7.3.3 `drivers/ps4/{sb-aeolia.c,ps4sb.h,sb-icc.c}` (from ~/Documents tarball;
  sha256 028873cb… / 0f98daa0… / 178e9a1e…; byte-identical to drive `strawberry/`)
- IOMEM = live `/proc/iomem`, Fat Aeolia, `macos-ps4/dumps/fat-aeolia/hwdump/proc-iomem.txt`
  (Linux 7.3.3 running under the 1 GiB VRAM loader)

## 1. Roadmap source citations — re-checked

| Roadmap claim | Result |
|---|---|
| `sb-aeolia.c:49-53` writes glue BAR mask + base | P — `glue_set_region()` writes `AEOLIA_REG_BAR_MASK` then `AEOLIA_REG_BAR_ADDR` through BAR4 |
| `sb-aeolia.c:504` MEM func BAR2 = `0xC0000000`/`0x3FFFFFFF` | P — exact line |
| `ps4sb.h:245-251` SPM ICC `0x2C000`, boot params `0x2F000` | P — "Relative to func6 BAR5"; `ICC_SPM_BASE` (line 42) duplicates the same value |
| `sb-icc.c:580-614` resolves MEM BAR5, reserves + ioremaps ICC SPM | P — `pci_resource_start(mem_dev,5)+ICC_SPM_BASE`, region "spm.icc" |

## 2. Two address spaces (the key finding)

The glue base values are **not** CPU physical addresses (P, by comparison of SRC vs IOMEM):

| Device | glue base (SRC) | CPU BAR (IOMEM) |
|---|---|---|
| xHCI (14.7) | 0xbf400000 / 0xbf600000 / 0xbf800000 | 0xdc000000 / 0xdc200000 / 0xdc400000 |
| GbE sky2 (14.1) | 0xbfa00000 | 0xc4000000 |
| AHCI (14.2) | 0xbfa04000 | 0xc8000000, 0xc8001000 |
| SDHCI (14.3) | 0xbfa80000 | 0xcc000000 |
| MEM (14.6) 1 GiB window | 0xc0000000 / mask 0x3fffffff | 0x80000000-0xbfffffff (I: same window, CPU side; BAR index U) |

Consequence: firmware must keep a **CPU-physical** map and a **southbridge-internal** map separately.
Never feed a glue base to the CPU or a CPU BAR to the glue.

**Caveat (critical for Directboot):** SRC *reprograms* these glue windows at init (`sb-aeolia.c:495-510`).
IOMEM shows the post-Linux state. Under OrbisOS at takeover the glue programming may differ → U until
the read-only diagnostic payload reads `AEOLIA_REG_BAR_MASK/ADDR` under OrbisOS.

## 3. Aeolia functions — CPU-side resources (IOMEM, P)

| Func | Resource | Notes |
|---|---|---|
| 14.4 glue | 0xd0000000 (1 MiB), 0xd0100000 (32 KiB, chipid @0xd0101000), **BAR4 0xd0200000-0xd03fffff (2 MiB)** | ICC regs 0xd0384000 = BAR4+0x184000 (`AEOLIA_RGN_ICC_BASE`, P); "aeolia.glue" 0xd03c8000 = BAR4+0x1c8000 (MSI region) |
| 14.6 MEM | 0x80000000-0xbfffffff (1 GiB), 0xd8000000 (4 KiB), **BAR5 0xd9000000-0xd903ffff (256 KiB) = SPM**, 0xd9100000 (1 MiB) | SPM: ICC 0xd902c000 (+0x000 request, +0x800 reply), boot params 0xd902f000 (0x20 B) |
| 14.0 | 0xfdf8000000-0xfdf9ffffff (32 MiB, above 4 GiB) | high BAR; purpose U |
| 14.0-14.7 | 64 KiB each at 0xfc080000…0xfc0f0000 | purpose U (per-function windows) |
| 14.5 | 0xd4000000, 0xd4001000 | function U |

## 4. Platform MMIO (IOMEM, P)

| Range | Owner |
|---|---|
| 0xe0000000-0xe3ffffff | GPU 00:01.0 (64 MiB aperture) |
| 0xe4000000-0xe47fffff, 0xe4800000-0xe483ffff | GPU MMIO (loader `configure_vram` uses 0xe4800000) |
| 0xe4840000-0xe4843fff | HDA 00:01.1 |
| 0xf8000000-0xfbffffff | PCI ECAM, bus 00-3f |
| 0xfc000000-0xfc07ffff | AMD IOMMU (loader writes control at 0xfc000018) |
| 0xfee00000 | LAPIC (MADT, ledger H3) |
| 0x80000000-0xfbffffff | Reserved umbrella for all of the above |

## 5. RAM (IOMEM) — usable-after-takeover, NOT OrbisOS ownership truth

System RAM: 0x1000-0x7fff, 0x80000-0x99fff, 0x100000-0x3fffff, 0x480000-0x67ffff,
0x700000-0x7efe7fff, 0x7f000000-0x7fffffff, 0x140000000-0x27effffff.
Reserved 0x100000000-0x13fffffff = the 1 GiB VRAM carve (matches loader `vram_base`).
ACPI: 0xe0000-0xeffff (the **loader-synthesized** tables), 0x7eff0000 tables, 0x7efe8000 NVS.

This is the e820 the Linux loader hardcoded (`prepare_boot_params`, "values from fw 1.01").
It proves those ranges were safe for Linux *after the loader's teardown*. It does not prove what
OrbisOS owns at takeover → Directboot's memory map stays U until read from OrbisOS.

## 6. Staging rules derived so far
1. SPM (0xd9000000, 256 KiB) is device-shared ICC memory → never a staging buffer.
2. The 1 GiB MEM window at CPU 0x80000000 is a PCI BAR inside the reserved umbrella → not RAM.
3. Firmware image + handoff block go in ordinary System RAM that OrbisOS has released (U: which).

## 7. What the read-only diagnostic payload must report (to close the U rows)
- glue `AEOLIA_REG_BAR_MASK/ADDR` for every function, under OrbisOS (before any reprogramming)
- PCI config BARs of 00:14.0-14.7 and 00:01.0 (CPU side, with BAR indices)
- OrbisOS physical memory map at takeover (phys_avail / smap)
- display scanout base (fb_phys) from the GPU display registers
- RSDP location and the original Sony ACPI tables
- IOMMU control/status, Aeolia MSI enable + status (0x1c8408 vs 0x1c843c question)

## 7a. Status after hardware run 2 (analysis/run2/FINDINGS.md)
| item | status |
|---|---|
| glue BAR_MASK/ADDR per function under OrbisOS | **answered** — identical to sb-aeolia.c for every window Linux writes; extra OrbisOS windows recorded |
| PCI config BARs 00:14.x and 00:01.0 | **answered** — same CPU BARs as Linux IOMEM; Aeolia aliases on all free device numbers |
| OrbisOS physical memory map | **partial** — DRAM 0–2 GiB + 4 GiB..0x27f000000; DMAP has holes; needs full DMAP PD dump |
| display scanout (fb_phys) | **inferred** 0x6e800000 (CRTC1 1920×1080) — needs on-screen proof |
| RSDP + original Sony ACPI | **answered** — 0xE0000 (SIE); byte-identical to what Linux received |
| IOMMU control/status | **answered** — active (ctrl 0x1c0d, exclusion over all), tables at 0x05318000 / 0x7e998000 |
| Aeolia MSI enable/status (H2b) | **values read**, meaning still open (needs a controlled experiment) |
