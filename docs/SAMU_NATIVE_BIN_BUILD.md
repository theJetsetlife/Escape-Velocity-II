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

## Independent and primary-source cross-check (2026-11-23)

**The wiki and Reddit are not the only sources.** Provenance matters:

1. **Primary disclosure:** [flatz's original 2021 C PoC](https://gist.github.com/flatz/22215327864d7512e52268f9c9c51cd8) establishes source functions and targeted service calls, but does not itself supply a standalone binary build contract.
2. **Contemporaneous reporting:** [BiteYourConsole, December 19, 2021](https://www.biteyourconsole.net/2021/12/19/scena-ps4-il-developer-flatz-lancia-la-bomba-un-nuovo-exploit-permettera-di-dumpare-le-chiavi-samu/) quotes flatz's original announcement and repeats that the bug was fixed in a firmware between 7.55 and 9.00. This is contemporaneous evidence for the claim, **not** an independent firmware diff.
3. **Independent implementation evidence:** [jocover/ps4-hen-vtx, `samu_key_dump` branch](https://github.com/jocover/ps4-hen-vtx/tree/samu_key_dump) provides a separate 5.05 source tree including `kpayload/Makefile`, `kpayload/linker.x` and `kpayload/source/samu_dump.c`. Its [`pfs_dump` release](https://github.com/jocover/ps4-hen-vtx/releases/tag/pfs_dump) includes flat `.bin` payload artifacts for 5.05 and 6.72. **These are derivative implementations, not proof that flatz's gist itself was a `.bin` distribution or that the same behavior works on 9.00.**
4. **Separate hardware research:** [John Fitzgerald, “PlayStation 4 Extraction,” November 2024](https://johnfitz.me/2024/11/22/playstation-4-extraction/) documents physical BootROM/eFuse research and acknowledges flatz's CCP bug. It does not corroborate a working firmware 9.00 CCP key extraction path.
5. **Wiki claim:** [PSDevWiki Vulnerabilities, Secure Modules — Patched](https://www.psdevwiki.com/ps4/Vulnerabilities) states that 7.55 was unpatched and 9.00 patched. No particular secure-kernel instruction diff or reproducible 9.00 test is supplied in that section.

**Evidence standard:** Treat `9.00 patched` as reported, not byte-proven for this console. An exact x86 kernel binary is **not** equivalent to an exact SAMU secure-kernel image; a Ghidra analysis of host-side code cannot by itself confirm secure-kernel patch instructions. Do not promote assertions from a secondhand article or wiki to local hardware-verified status.
