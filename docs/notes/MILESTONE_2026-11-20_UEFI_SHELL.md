# MILESTONE 2026-11-20: PS4 Fat 9.00 directboot -> EDK II UEFI, successful build

## Status
- **Successful PS4 run: 07:59 UTC, 2026-11-20** (reported successful by the operator; receiver log
  `reports/diag-20261120-155920.txt`: arm OK, hook installed with all 8 CPUs stopped, wlanbt(2), takeover).
  Photo/video of this run: not yet archived; add it under the milestone backup `evidence/` when available.
- Chain: WebKitty jailbreak -> GoldHEN slot payload = directboot stage -> ring0 arm (staged, flushed per step)
  -> hook icc_query_nowait (all CPUs stopped) -> takeover of OrbisOS -> 30 s hold -> EDK II Universal Payload
  -> DXE -> BDS -> UEFI Shell, drawn on the PS4 scanout by Ps4GopDxe.

## Exact artifacts (sha256)
| artifact | sha256 | size |
|---|---|---|
| stage `src/directboot/directboot.bin` = WebKitty slot `includes/payloads/HEN/goldhen_v2.4b18.14.bin` | ff86e20d802e035b706bcea8fd21f6688f3b16dae10173bf61ba8d529b815e68 | 29584 |
| UEFI payload `out/upl-ps4.elf` (served by diag-receiver as /upl/upl-ps4.elf) | 5c32a84d5613ae7b551418322f8d0f7ed10ad21db42516286b0a7f984b6bccb7 | 4226976 |
| edk2 upstream (unmodified, clean tree) | commit 2970e5699ba6267f3384ffab20f96647578aebc8 (https://github.com/tianocore/edk2.git) | |
All PS4 changes to EDK II live in `Ps4Pkg/` (drivers, libraries, and `mkplatform.py`, which generates
`Ps4Payload.dsc/.fdf` from upstream UefiPayloadPkg by exact-match edits). Receiver: 10.0.0.71:9031.

## What got us here (each blocker: evidence -> cause -> fix)
1. **arm hang**: M_WAITOK on large contiguous allocations killed the console (3/3 runs) -> M_NOWAIT +
   userland retry (DB_ARM_RETRY, 40 x 250 ms); heap code gated off.
2. **no large contiguous blocks**: 8.1 MiB region / 7.9 MiB framebuffer failed -> region 1 MiB with the FVs
   used in place in the ELF copy; framebuffer falls back to the reused OrbisOS scanout (0x6e800000 / 0x6e008000).
3. **power-off during the hold** (06:08 run): clearing Bus Master on live devices -> removed; reference
   loader step wlanbt(2) (kbase+0x180860) added as the last ring0 call.
4. **HDMI audio DMA** kept running (IOMMU off -> raw physical writes) -> `hda_stop()` on 00:01.1
   (Linux snd_hdac sequence), marker slot 11 green.
5. **PciBusDxe** sized BARs / toggled command registers of live devices -> removed from the FDF;
   **Ps4GopDxe** rewritten standalone: GOP from gEfiGraphicsInfoHobGuid on its own handle (vendor device
   path gPs4GopDevicePathGuid), adds itself to ConOut/ErrOut. Proven on HW: BDS prompt drawn (07:21 run).
6. **DCE probe** (db_probe, 2 ring0 syscalls before arm) removed entirely (operator decision); the takeover
   always uses the INUSE/OVL/MASTER registers, as in every run since run 3.
7. **Moving deaths** (32.96 s CpuDxe / 37.47 s BDS / 39.5 s PciBus, three different steps): DxeMpInitLib
   broadcasts INIT-SIPI-SIPI to all APs even with MAX_LOGICAL_PROCESSORS=1 and sizes AP stacks for 1 CPU,
   so the 7 parked PS4 cores restarted inside EDK II and corrupted memory. Reproduced in QEMU -smp 8.
   Fix: **MpInitLibUp** (UPL 57ddcb99). 07:35 run: no death, screen on.
8. **Stuck at `[Bds]BdsWait(3)`** (07:35 run, first wait that needs a timer interrupt): the takeover can run
   inside an OrbisOS interrupt handler, so that vector stays in service (no EOI) and blocks the EDK II timer
   (vector 0x20). Reproduced in QEMU (STALE_LAPIC=1: 0x40 in service + 0x30 pending -> same line).
   Fix: **Ps4ApicFixupDxe** (UPL 5c32a84d): temporary EOI handlers 0x21-0xFF, logs SVR/TPR/PPR/ISR/IRR,
   TPR=0, APIC software enable, EOI until ISR empty, 2 ms with interrupts on to absorb pending vectors.
   QEMU: 1 EOI + absorbed 0x30 -> UEFI Shell. **07:59 run: success.**

## Rules learned (keep)
- Never let firmware touch PCI config (BARs, command register, Bus Master) of devices OrbisOS left running.
- Never let EDK II send INIT/SIPI to the PS4 APs (MpInitLibUp only).
- The BSP's local APIC is inherited dirty: clear ISR/TPR and absorb IRR before relying on timer interrupts.
- No M_WAITOK allocations inside ring0 arm; every arm step is its own syscall, flushed to the receiver.
- Test every UEFI/handoff change in `qemu/` first: `./run.sh` (default SMP=8) must print `RESULT: PASS`;
  also `STALE_LAPIC=1 ./run.sh` and `SMP=1 ./run.sh`. In zsh, set env vars explicitly (no `env $cfg`).
- Review every frame of a run video, especially the end (a 2 fps sample once missed a black-out).
- Never alter WebKitty code (swap the slot payload only); never run webkitty-docker/run.sh; never probe
  GoldHEN BinLoader 9090.

## Rebuild / deploy / test
```
cd rebuild/src/directboot && make PC_IP=10.0.0.71 PC_PORT=9031      # stage -> directboot.bin
cd rebuild/Ps4Pkg && ./build.sh                                      # UPL -> ../out/upl-ps4.elf
tools/upl_hosttest out/upl-ps4.elf                                   # host checks
cd rebuild/qemu && ./run.sh 40 && STALE_LAPIC=1 ./run.sh 40 && SMP=1 ./run.sh 40
cp src/directboot/directboot.bin ../../WebKitty-directboot-test/includes/payloads/HEN/goldhen_v2.4b18.14.bin
```

## Open items (not blocking the milestone)
- 30 s hold (DB_HOLD_SECONDS) still in the stage; can shrink now.
- fb-console debug text overlaps the Shell banner (same framebuffer); cosmetic.
- QEMU harness `pmemsave` of the fbcon descriptor is flaky (diagnostic only).
- Input (keyboard/controller), storage (AHCI behind Aeolia), USB: not yet.
- Archive the photo/video of the successful run.
