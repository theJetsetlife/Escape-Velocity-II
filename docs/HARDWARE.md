# Running on a PS4

## Requirements
- PS4 "Fat" (CUH-1000/1100/1200 series, Aeolia southbridge) on system software **9.00** (the only version the stage
  arms; others are recognised and refused — docs/FIRMWARE_VERSIONS.md).
- A PC on the same LAN, reachable from the PS4 on the web port (default 9898) and the receiver port (9031).
- A WebKitty checkout (https://github.com/ArabPixel/WebKitty, tested at commit `ca35118`) — used unmodified;
  only `includes/payloads/HEN/goldhen_v2.4b18.14.bin` is replaced by the stage.
- Camera for the screen: every stage before EDK II reports by coloured squares; film the whole run.

## Procedure
```sh
cp server/.env.example server/.env    # PC_IP, PC_PORT, WEB_PORT, WEBKITTY_DIR
make stage upl                        # or use prebuilt/ (retarget a prebuilt stage: tools/diag_recv.py patch)
make server                           # containers: webkitty (:9898) + diag-receiver (:9031, serves out/upl-ps4.elf)
make deploy                           # stage into the slot; prints DEPLOYED when both served hashes match
docker logs -f diag-receiver          # watch the stage's reports
```
On the PS4: open `http://<PC_IP>:9898/` in the browser (first visit caches the site), keep GoldHEN selected as the
HEN, run the jailbreak. From there it is automatic: the stage reports, downloads the UEFI payload, arms, and
restarts OrbisOS itself; at shutdown the hook takes over. Reports are saved in `reports/diag-*.bin/.txt`.

## Reading a run
- Squares (top-left): blue = hook reached; white/yellow = APs parked / timed out; green 1–3 = takeover, quiesce,
  memory map (yellow 3 = reserved ranges dropped); blue 4 = jumping into EDK II; red 5 = `upl_load` failed.
  Table: `src/directboot/README.md`.
- After square 4, EDK II's DEBUG output scrolls on the screen (fb console), then the UEFI Shell appears.
- In the Shell: `ps4log` writes everything (boot log, hardware, network) to a FAT USB stick in one file. With the
  current tree a `boot-*.txt` copy is written to the stick automatically at every boot.
- Review every frame of the video, including the very end.

## Safety notes
- The stage writes no PCI configuration of live devices and refuses non-9.00 kernels; keep it that way.
- Never connect to GoldHEN's BinLoader port 9090 to "check" it; never send a payload twice.
- A failed run normally ends in a black screen or a power-off; hold the power button to recover. No run so far
  has needed more than a power cycle.
