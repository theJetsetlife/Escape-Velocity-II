# diag — read-only diagnostic payload (roadmap phases 01 + 02)

Purpose: prove delivery → userland execution → ring0 on the real Fat (9.00), and measure
the facts the memory/glue contract still marks U/RED, **without writing to any hardware**.

## Run it (WebKitty's own Bin Loader chain — WebKitty is reused unchanged)
1. PC: `rebuild/webkitty-docker/run.sh` — serves your WebKitty folder read-only on `:9898`
   (python http.server) and starts the report receiver on `:9031`.
2. PS4 browser: `http://10.0.0.71:9898/` → **Tools → Bin Loader**. WebKitty runs the 9.00 chain
   and ends with "awaiting payload on port 9020".
3. PC: `python3 rebuild/tools/diag_recv.py send` — sends diag.bin ONCE to 10.0.0.12:9020.
   Never probe 9020 first: the loader executes whatever arrives, an empty connect = empty buffer.
4. Report: `docker logs -f diag-receiver`, files in `rebuild/reports/`.

The Bin Loader (bundle.js runBinLoader) reads the blob into an RWX buffer and calls offset 0
with no arguments — the same entry contract as diag.bin's `_start`.

PC address: `make PC_IP=a.b.c.d PC_PORT=n`, or patch a built blob without rebuilding:
`diag_recv.py patch diag.bin --ip a.b.c.d`. Default is 10.0.0.71:9031 (wifi).

## Staging format (A7; the 9020 Bin Loader calls offset 0 the same way)
Flat blob, entry at offset 0 (`jmp` over the config block), config block at 0x10:
`"DBDIAGCF" ip[4] port[2] rsvd[2]` (network order). Position independent, no relocations
(the build fails if any object has an absolute/GOT relocation), `.bss` folded into the file.

## Execution trace
`connect` → `HELLO` → sysctls (`machdep.smap`, `vm.phys_segs`, `machdep.acpi_root`, `hw.*`)
→ CPUID SMEP/SMAP gate → for each phase: `STAGE enter` is sent and given 150 ms on the wire,
then `kexec(ring0_main)`, then `RET` + `STAGE leave` are sent. If the console dies, the last
`STAGE enter` the PC received names the phase (the receiver prints this).

| phase | reads | gate before reading |
|---|---|---|
| 1 ident | LSTAR → kbase; kernel bytes at +0x1c0/+0x4c7ad/+0x2e1760; DMAP globals | all three signatures must match 9.00 or **everything stops**; DMAP self-check (kernel text via page walk == via DMAP) |
| 2 cpu | CR0/3/4, EFER, CPUID, APIC_BASE, MTRRs, PAT, SYSCFG, TOP_MEM, TOM2, MMIO_CFG_BASE | — (MSRs that exist on fam16h only) |
| 3 pgtbl | raw PML4, DMAP PDPTs, leaf + memtype for 15 probe addresses | page walk |
| 4 pci | bus 0 config space, 256 B per function | ECAM 1 MiB must be mapped + UC |
| 5 glue | 14.4 BAR4+0x1c8000 (windows), +0x1c8400 (MSI), BAR2+0x1100 (chip id) | mapped + UC (MTRR UC, PAT ≠ WC) |
| 6 iommu | 00:00.2 cap 0x0f → base; +0x0..0x7f, +0x2000..0x203f | mapped + UC |
| 7 gpu | 00:01.0 BAR5: MC_VM_* + 6× CRTC primary surface regs | mapped + UC |
| 8 acpi | RSDP (sysctl, then 0xE0000 scan), RSDT/XSDT, every table, DSDT, FACS | mapped, length ≤ 256 KiB |

Not done on purpose: CF8/CFC port config cycles (would write the address latch), any
read of ICC/SPM registers, anything outside bus 0, any write at all.

## Failure codes (`T_ERR`, see diag.h)
1 FW_GATE · 2 DMAP · 3 NOT_MAPPED · 4 NOT_UC (skipped) · 5 NO_ECAM (PCI phases skipped) ·
6 NO_DEV · 7 NO_BAR · 8 NO_RSDP · 9 ACPI_BAD · 11 KEXEC_RET · 12 BUFFER_FULL.
No connection to the PC → the payload returns without entering ring0.

## Status
Builds clean (`-Werror`, btver2, general-regs-only, no SSE, no wrmsr/out/CR writes in the
disassembly). **Not yet run on hardware.** Compiling ≠ working.
