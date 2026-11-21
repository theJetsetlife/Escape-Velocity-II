# The working reference chain → Directboot (from ~/Documents/claude review)

The ps4-linux-loader (`~/Documents/claude/ps4-linux-loader-master.zip`, extracted read-only to
`ref/linux-loader-zip/`) is the proven OrbisOS → new-kernel takeover on these consoles. Directboot is
**the same chain with EDK II (UPL) in place of bzImage**. We reuse its architecture stage for stage.
Offsets below were re-checked against the retail 9.00 dump (`objdump`), and agree with YoRHa `fw900.h`.

| # | Linux loader (main-aio.c / ps4-kexec-common) | 9.00 evidence | Directboot equivalent | status |
|---|---|---|---|---|
| 1 | WebKitty bundle chain → payload in GoldHEN slot (pthread, offset 0) | CHAIN.md, A10 | same | **proven on HW** (diag runs 1–3) |
| 2 | `kexec(kernel_main)` = syscall 11 → ring0 | A1–A3, A13 | same | **proven on HW** |
| 3 | `kernel_main`: kbase = LSTAR − 0x1c0; CR0.WP off; patch1 +0x37BF3C = 7, patch2 +0x37BF44 = 7 (kmem_alloc's two `vm_map_insert` prot args → RWX); pstate +0x29A970 = 3 (arg of set_pstate +0x4D6FC0 on the shutdown path) | dump disassembly: both prot immediates sit exactly there (already 7 in the dump = dumped from a patched kernel); pstate imm is `mov $1,%edi` before `call set_pstate` | same patches | verified offsets, not yet run by us |
| 4 | `kmem_alloc(*kernel_map +0x2268D48, size)` (+0x37BE70) → copy kexec blob, call it with `printf` (+0xB7A30) | prologues verified; YoRHa agrees | our ring0 stage blob | verified offsets |
| 5 | kexec blob `kexec_init`: resolves symbols, installs **syscall 153** `sys_kexec` | sysent +0x1100310 (YoRHa + loader agree; dump region zero → A4 stays RED until read live) | our syscall 153 (`sys_directboot`) | — |
| 6 | userland reads files, `kexec_load` (syscall 153): `kernel_alloc_contig` = `kmem_alloc_contig` (+0x270880) + `pmap_extract` (+0x12D050) → **physical contiguous staging via OrbisOS** (answers run 3's "no fixed range") | prologues verified | stage UPL ELF + HOB inputs the same way | — |
| 7 | GPU clocks/pstate set; `kernel_hook_install(hook, icc_query_nowait +0x2E1760)` | D1–D3 | same hook | verified offsets |
| 8 | reboot thread: `evf_open("SceSysCoreReboot")`, `evf_cancel(0x4000)`, `kill(1, SIGUSR1)` → OrbisOS shuts down → `shutdown_final` → hooked `icc_query_nowait` | D2 | same | — |
| 9 | hook: `wlanbt(2)`, `fix_acpi_tables(0xE0000)`, `smp_rendezvous(…, cpu_quiesce_gate, …)` | ACPI at 0xE0000 is Sony's (run 2) | same; ACPI passed to UPL via HOB | — |
| 10 | `cpu_quiesce_gate` per CPU: LAPIC reset (0xfee00410 / LVT / IRR), MTRR re-setup; APs `cpu_stop`; BSP waits for 7 | 8 CPUs (E1), MTRRs (H16) | same | — |
| 11 | BSP: identity 1 GiB maps, new PML4 at **phys 0x1000**, GDT, thunk in low memory | — | page tables for UPL entry (64-bit, identity) | — |
| 12 | **IOMMU off**: `*(u64*)0xfc000018 &= ~1` | run 2: IOMMU active, ctrl 0x1c0d | same | — |
| 13 | **Aeolia MSIs off**: `*(u32*)(0xd03c844c + 4i) = 0`, i = 0..7 | **resolves H2b**: these are MSI_MASK(func) (ps4sb.h); run 2 values are non-zero exactly for the functions with live MSIs (1–5, 7) | same | — |
| 14 | **HPET stop**: 0xd0382010, 0xd0382100/120/140/160 = 0 (BAR4 + 0x182000) | not read yet | same | RED |
| 15 | `configure_vram` (MC_VM_FB_LOCATION / FB_OFFSET / HDP / CONFIG_MEMSIZE at 0xe4800000) | run 2: Orbis FB range empty, system aperture to 0x47fffffff | GOP needs only the scanout, not a VRAM carve | decide later |
| 16 | GPU soft-reset (0xe48086d8, 0xe4808234, 0xe480d048/d848, 0xe480c300, 0xe4808020, 0xe4800e60) + HDMI audio pins | — | **keep the scanout alive** instead: we want CRTC1 + current surface for a GOP | design choice |
| 17 | `jmp_to_linux(image, bootparams, pml4, gdt)` | — | `upl_load` → UPL entry, RCX = HOB list (G1–G3) | src/upl_load.c built |

## What this changes
- Staging (phase 03) is solved the way the reference does it: `kmem_alloc_contig` + `pmap_extract`.
- H2b closed by the working loader + run 2 values: MSI disable = zero the 8 words at glue BAR4+0x1c844c.
- The remaining unknowns are hardware behaviour on *our* path (HPET, keeping the display alive
  through takeover), not addresses.
- Loader README warning: "loading this loader through a webkit exploit will crash the webkit process".
  Our payloads already run fine in the WebKit process (runs 1–3); the reboot/kexec stage must be tested
  for this specifically.

## Other material reviewed in ~/Documents/claude
- `ps4-payload-sdk-main.zip` → `ref/payload-sdk-zip/`: libPS4, `crt0.s` = `jmp _main` at offset 0,
  linker.x puts `.text.start` first — the same entry contract diag.bin uses.
- `yorha-build/` (YoRHa kernel debugger, fw 9.00/9.03): offsets agree with the loader (printf, kmem_alloc,
  kmem_free, kernel_map, sysent, copyin). Its `kpmap_extract` +0x12d3b0 is a *different* function from the
  loader's `pmap_extract` +0x12D050 (it reads the DMAP globals first → likely `pmap_kextract`).
- `SDK/12.508/extracted/`: NID database, module/stub-library lists, libc/libSceSecure decompiles —
  userland reference (e.g. sceSysUtilSendSystemNotificationWithText for on-screen notifications).
- `linux-ps4-7.3.3.*`: the same source already in `ref/linux-ps4-7.3.3`.
