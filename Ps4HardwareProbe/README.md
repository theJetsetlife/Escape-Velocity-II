# Ps4HardwareProbe

A standalone, read-only hardware reconnaissance payload for the PS4 family, built with the PS4 Payload
SDK and loaded by GoldHEN's BinLoader. It identifies the console from hardware identities (GPU and
southbridge PCI ids, chip id registers, CPUID), the firmware from `libc.sprx` and the live kernel, and
reports every diagnostic as **OK / UNSUPPORTED / SKIPPED (safety) / FAILED** with a reason, to a PC.

It does not depend on Linux, EDK II, OpenCore or Directboot. It is the OrbisOS-side tool next to the
firmware project in this repository.

**Status: builds and passes the mock-hardware host tests. It has not been run on a physical PS4.**
See [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) for the model-by-model matrix.

## Build

```sh
make -C Ps4HardwareProbe            # -> payloads/Ps4HardwareProbe.bin (+ .sha256), then image checks
make -C Ps4HardwareProbe hosttest   # mock-hardware tests (17 scenarios + loader contracts) + decoder test
make -C Ps4HardwareProbe fwident    # regenerate identity profiles from OpenGoldHEN (KDUMP= checks them on 9.00)
make -C Ps4HardwareProbe hosttest KDUMP=/path/kernel_900_full.bin   # 9.00 scenarios on the real kernel image
make -C Ps4HardwareProbe PC_IP=10.0.0.71 PC_PORT=9033 ENV=2 LEVEL=1 STRICT=0
```

Needs gcc, binutils (objcopy, readelf, objdump), make, python3. The PS4 Payload SDK is taken from
`PS4SDK` (default `reference/payload-sdk-zip/ps4-payload-sdk-main`) and built out of tree in `build/`.

`make check` (part of `make`) fails the build unless: the entry at offset 0 is a `jmp` and the config
block is at 0x10; the linked image has no absolute/GOT relocations or dynamic sections; there is no
`wrmsr`, `out`/`outs` or move to a control register anywhere in the linked code; ring0 objects never
reference libc.

## Run

1. PC: `python3 Ps4HardwareProbe/logging/probe_recv.py listen --port 9033 --out reports/probe`
2. Optional: retarget a built binary without rebuilding:
   `probe_recv.py patch payloads/Ps4HardwareProbe.bin --ip <PC> --port 9033 --env goldhen --level 1 --strict 0`
3. Console: start GoldHEN's BinLoader, then from the PC send it **once**:
   `probe_recv.py send payloads/Ps4HardwareProbe.bin --host <console> --port 9090`
   (never connect to the BinLoader port for any other reason: an empty connection crashes it)
4. Reports: `reports/probe/probe-*.txt` (human), `.json`, `.bin` (raw; `probe_recv.py decode` / `compare`).

Options in the config block: `env` (loader: goldhen / webkitty; undeclared = payload does nothing),
`level` (0 config space only, 1 + windows already read on real hardware, 2 + windows public drivers read),
`strict` (default 0: community profiles are used after runtime validation; 1 = only hardware-verified
offsets), `fw` (operator-declared version, cross-checked against libc.sprx).

## Layout

| Dir | Contents |
|---|---|
| `core/` | wire format (`probe.h`), read-only HAL (`hal.h`), safety gates, ring0 dispatcher, SDK userland entry, crt0 + config, linker script |
| `platform/` | console-family profiles (`fat/`, `slim/`, `pro/`) and identification (`detection/`) |
| `southbridge/` | Aeolia, Belize, Baikal backends (ids, allowlisted windows with levels and evidence) |
| `firmware/` | detection (libc.sprx + live kernel), profiles (vendored offset table), validation (direct-map proof) |
| `adapters/` | GoldHEN and WebKitty loader contracts |
| `diagnostics/` | cpu, memory, acpi, pci, interrupts, gpu, usb, storage, timers |
| `logging/` | record buffer, PC receiver / decoder / comparer / patcher, decoder test |
| `tests/` | mock hardware (`mock_hal.c`) and scenarios (`probe_hosttest.c`) |

Adding a console revision = a board or southbridge data entry; adding a firmware = a verified row in the
offset table. The core is not rewritten for either.

## Safety

Read-only by construction: the HAL has no write primitive. Every physical read is page-walked first; every
MMIO read additionally needs MTRR UC and PAT not WC; model-specific MSRs only on family 16h; PCI config only
through ECAM (no CF8/CFC latch writes); never ICC or SPM; no BAR sizing, no controller resets, no DMA, no
interrupt routing changes, no storage access. Kernel offsets are used only for the exact version detected
(never a nearest match) and only after the live kernel matches every code-site signature of that version;
an unknown version reads nothing of the kernel beyond the 8-byte Xfast_syscall signature. No exploit is
included: ring0 comes from the loader's existing kexec.
