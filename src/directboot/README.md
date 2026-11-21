# directboot — OrbisOS 9.00 → EDK II (upl-ps4) on the PS4 Fat

Same chain as the working ps4-linux-loader (docs/notes/REFERENCE_CHAIN.md), with our UEFI payload in place of bzImage.

## Run (fully automatic after the jailbreak)
See docs/HARDWARE.md. In short: `make stage upl server deploy`, then on the PS4 open
`http://<PC_IP>:9898/`, keep GoldHEN selected, jailbreak. The stage logs to the receiver
(`docker logs -f diag-receiver`), fetches the UPL, arms, and restarts OrbisOS itself (`kern_reboot`).
At shutdown the hook takes over.

## On-screen progress (squares, top-left, left to right)
| # | colour | meaning |
|---|---|---|
| 0 | blue | hook reached (scanout found) |
| 0 | white / yellow | all 7 APs parked / timed out waiting for APs |
| 1 | green | boot CPU starts takeover |
| 2 | green | IOMMU, Aeolia MSIs, HPET, GPU engines quiesced |
| 3 | green | memory map built |
| 4 | blue | own page tables + GDT active, calling upl_load |
| 5 | red | upl_load returned an error (stage halts) |
After 4, EDK II owns the screen (Ps4GopDxe on the same scanout).

## Pieces
- `user.c` — log channel, `DBUPLGET` fetch, TSC sample, `kexec(db_arm)`, `kexec(db_reboot)`.
- `arm.c` — gates (9.00 identity, DMAP proof, byte-exact patch sites), kmem_alloc RWX patches, kernel copy
  of this blob, kmem_alloc_contig staging (+pmap_extract), scanout at arm, hook install.
- `hook.c` — scanout, smp_rendezvous gate (LAPIC quiet, scanout MTRR UC, AP park), device quiesce,
  memory map (TOP_MEM/TOM2 + DMAP cache types), identity page tables + GDT, `upl_load`.
- `../upl_load.c` — ELF load, relocations (from the file via section headers, as edk2 ElfLib), FVs, HOBs.
  Host-tested against the real payload: `tools/upl_hosttest.c` (all checks pass).
