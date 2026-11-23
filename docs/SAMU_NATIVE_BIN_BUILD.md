# Standalone PS4 native .bin research track (no UEFI)

**Target:** PS4 Fat / Aeolia, firmware 9.00. **Host:** CachyOS (Arch Linux family).

## What flatz's gist actually provides

[Original `ccp_crypto_bug.c`](https://gist.github.com/flatz/22215327864d7512e52268f9c9c51cd8) contains two functions, `dump_raw_keys` and `dump_gen_keys`, and references external SBL/CCP services. The gist does **not** establish an original final output format; it provides no standalone entry point, linker script, make target, or payload loader contract. Do not claim the original PoC shipped as a `.bin` without independent build evidence.

A C source file (`.c`) may compile to a relocatable object (`.o`), be linked into an ELF, and be converted to a flat `.bin` where the intended loader requires one. A `.bin` has no ELF metadata, so entry offset, position independence, and complete linkage are required.

## Existing standalone binary route

The repository **already** has a native, non-UEFI flat diagnostic payload:

- `src/diag/Makefile`: target `diag.bin`.
- `src/diag/diag.ld`: `ENTRY(_start)` and image origin zero.
- `src/diag/entry.S`: loader entry.
- `src/diag/README.md`: runtime contract and receiver logging.

The toolchain is Clang + LLD + llvm-objcopy, using x86_64 freestanding code. Its build rejects absolute/GOT relocations and emits a SHA-256 sidecar. It does **not** require `make setup`, `make upl`, `Ps4Pkg/` or an EDK II image.

## Install on CachyOS

```bash
sudo pacman -Syu
sudo pacman -S --needed git python clang lld llvm make
```

From an Escape Velocity II checkout:

```bash
make -C src/diag PC_IP=192.168.1.10 PC_PORT=9031
file src/diag/diag.bin
(cd src/diag && sha256sum -c diag.bin.sha256)
```

Replace `PC_IP` with the address of your actual PC receiver. The `file` utility cannot infer architecture or entry point from a raw binary; the `.elf` intermediate and linker layout are the authoritative structural evidence. A successful build does **not** prove hardware operation.

To use the synthetic, **host-side** model independently:

```bash
mkdir -p logs
python3 tools/samu_hmac_policy.py | tee logs/samu_hmac_report.json
python3 -m unittest discover -s tests -p 'test_samu_hmac_policy.py' -v 2>&1 | tee logs/samu_hmac_tests.log
```

These host commands do not build or run a PS4 payload; the native `diag.bin` and the Python model are separate deliverables.

## Operational notes

Read `src/diag/README.md` and `docs/HARDWARE.md` before running anything on a console. This repo reports the hardware-verified UEFI Shell boot pair separately from the `src/diag` track. Do not alter or delete the verified UEFI artifacts simply because this research track does not use them.

The native diagnostic payload reports non-secret system information to the configured receiver, **not** SAMU cryptographic keys. No SAMU key-extraction calls, private-key export, or exploit chain are included. Firmware 9.00 behavior is not established by the historical 7.55 proof of concept.
