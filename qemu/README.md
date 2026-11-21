# QEMU handoff harness

Runs the directboot handoff with the real UEFI payload, off the console:
`stub.S/stub.c` (multiboot, 32-bit) -> long mode -> `h64.c` (64-bit), which links the stage's
**unchanged** `src/upl_load.c` and `src/directboot/memmap.c`, fills `struct upl_inputs` the way
`hook.c` takeover does, and jumps into `out/upl-ps4.elf`.

    ./run.sh [WAIT=40]          # UPL=<elf> to test another payload
    ACCEL=tcg QCPU=max QEMU_EXTRA="-d int,cpu_reset -D $XDG_RUNTIME_DIR/dbq-int.log" ./run.sh 150

Outputs in `out/`: `debug.log` (stub/h64 lines + every EDK II DEBUG line, mirrored to port 0xE9
because h64 sets `PS4_FBCON_FLAG_DEBUGCON`), `screen.png` (the fb console after WAIT s),
`fbcon.txt` (descriptor at 0x8000), `monitor.txt` (status, registers, IDT gates, TSS).

Pass = `debug.log` reaches `[Bds]Booting UEFI Shell` / Shell.efi and the VM is running (HLT idle).
First pass: 2026-11-20, UPL e5356dbf, Shell protocol installed at 0.775 s.

## Mirrors the PS4 stage
- ELF copy at 0x800000 (arm4), region 0x12e00000 / 1 MiB (arm5), boot block 0x34d70000 reserved,
  FVs in place in the ELF copy, fb console descriptor v2 at 0x8000 (timestamps, page mode, cursor).
- Memory map from `memmap.c` with a PS4-shaped phys_avail (0x700000-0x9400000, 0x14928000-0x57738000).
- IDTR = {0, 0} at handoff, as `switch_tables` does. EDK II's `InitializeCpuInterruptHandlers`
  copies the OLD IDT and keeps its IST bits; with SeaBIOS's IVT left in IDTR the timer gate (0x20)
  got IST3 -> stack 0 -> #PF on the first timer interrupt (found here, 2026-11-20).
- SSE on (CR4.OSFXSR/OSXMMEXCPT), CR0.TS/EM clear, as OrbisOS leaves them.
- PCI root bridge: bus 0, q35 BARs (the PS4 windows are the default in upl_load.c).

## Not reproduced (PS4-only)
Aeolia, no 8259/IOAPIC (LINT0), device quiesce, IOMMU, OrbisOS, Liverpool GOP (Ps4GopDxe binds the
PS4 GPU only, so the Shell's own text has no GOP here; the debug console shows the boot).
