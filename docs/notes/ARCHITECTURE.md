# Target architecture — mapped to the ledger

Status per layer: facts = LEDGER.md ids (GREEN/RED), code = files in src/.

| # | Layer | Facts | Code | Gap |
|---|---|---|---|---|
| 1 | PS4 Fat, fw 9.00 | H1 H3 H4 GREEN | — | — |
| 2 | WebKitty (PSFree + Lapse) | A1 (installs sysent[11]) GREEN | reference only | — |
| 3 | PS4 Payload SDK | — | reference only (SOURCES.md); ps4.h carries what we need | decide: link libPS4 or stay SDK-free |
| 4 | Kernel execution interface (syscall 11) | A1 A2 A3 A5 GREEN | ps4.h `kexec()` | entry.S stub not written |
| 5 | Native Directboot entry | D1 D2 D3 GREEN (ICC hook point); B1 B2 C2 RED | — | our kernel_main, hook install, takeover |
| 6a | CPU / SMP / LAPIC | E1 GREEN (8 cores, LAPIC 0xFEE00000) | — | rendezvous + AP park |
| 6b | Physical memory / MMU | RED — **no Linux e820 in Directboot** | — | memory map must come from OrbisOS |
| 6c | Aeolia MSI / AMD-Vi | E2 H2 RED | — | quiesce order + correct mask register |
| 6d | PCI / MMIO | ECAM 0xF8000000-0xFBFFFFFF seen in HWDUMP iomem (not yet ledgered) | — | ledger it; root-bridge HOB |
| 6e | ACPI / SMBIOS | H3 MADT GREEN; RSDP location RED; SMBIOS unknown | — | find RSDP from Orbis; SMBIOS likely absent |
| 6f | GPU / framebuffer | H1 geometry GREEN; **fb base RED — no amdgpu in Directboot** | — | read scanout base from Orbis display regs |
| 6g | USB / storage / power | — | — | EDK II DXE drivers, after handoff |
| 7 | EDK II UEFI | G1 G2 G3 GREEN | upl_hob.h, upl_load.c (compile clean, untested) | upl_inputs caller, link, PS4 test |
| 8 | OpenCore | — | — | out of current scope |
| 9 | macOS Monterey | — | — | out of current scope |

## Split of layer 6

Layer 6 is two different things:

- **Before EDK II (our handoff code, runs once):** park APs, own page tables, build the memory map,
  quiesce DMA/MSI/IOMMU/GPU engines, locate ACPI + framebuffer, fill the HOBs. → 6a, 6b, 6c (quiesce), 6e, 6f.
- **Inside EDK II (DXE drivers, run for the life of firmware):** PCI enumeration, Aeolia MSI routing,
  xHCI/AHCI/SD, power. → 6c (routing), 6d, 6g.

Route A got 6b, 6e and 6f for free from Linux (e820, RSDP, amdgpu fb_phys via shim_params).
Directboot has no Linux, so those three must now be read from OrbisOS state at takeover.

## Open decision: takeover timing
- ICC `shutdown_final` hook (D1-D3): OrbisOS has already run driver shutdown — orderly, proven by the Linux loader.
- Immediate syscall-11 takeover: earlier, simpler, but OrbisOS DMA/IRQ sources are still live.
