# Standalone native PS4 `.bin` scaffold (no UEFI)

This module produces a **real, linked 64-bit x86 flat `.bin`** with a WebKitty-compatible entry at offset 0. It is an executable **generic research scaffold**, not a SAMU exploit, key dumper, kernel patcher or proof of operation on a PS4. The extension function deliberately returns `RESEARCH_STAGE_UNIMPLEMENTED`. Neither EDK II nor the Linux kexec shim is involved.

## Building on CachyOS

```bash
sudo pacman -S --needed clang lld llvm make binutils python

# From Escape Velocity II repository root
make -C src/native-research PC_IP=192.168.1.50 PC_PORT=9031
python -m unittest discover -s tests -p 'test_native_research_build.py' -v
```

Replace `192.168.1.50` with your PC's receiver IP. The build's default is `10.0.0.71`, solely for compatibility with the existing repository diagnostic defaults; do **not** assume it is your actual PC IP. The entry assembly is recompiled whenever you run `make` to incorporate address changes.

Outputs:

- `src/native-research/native-research.bin`: flat 64-bit executable blob, entry at byte zero;
- `src/native-research/native-research.elf`: link intermediate for inspecting the entry point and symbols;
- `src/native-research/native-research.bin.sha256`: output checksum.

The build checks for absolute/GOT relocations, dynamic sections, and vector instructions. The image contains its own `.bss` bytes in the file to match the WebKitty Bin Loader's copy contract. It uses the same config block (`DBDIAGCF`, offset `0x10`) and diagnostic wire format (`DBDIAG01`) as `tools/diag_recv.py`.

## Custom implementation points

`src/native-research/experiment.c` contains three separate extension points: `research_prepare()`, `research_execute()` and `research_finish()`. The public `research_stage_run()` calls them in order. The deliberately empty execution function returns:

```c
static int research_execute(void)
{
    return RESEARCH_STAGE_UNIMPLEMENTED;
}
```

For generic non-secret diagnostic notes, this module exposes `research_stage_note(const char *message)` (maximum 128 bytes per note). The scaffolding invokes the extension, marks it as unimplemented in the report, and returns to its loader. No privileged service bindings or protected-key extraction interfaces are supplied.

## Receiving logs on a PC

Start the existing receiver **before** launching the payload:

```bash
python3 tools/diag_recv.py listen --port 9031 --out reports/native-research
```

In the existing PS4 WebKitty Bin Loader workflow (firmware 9.00), after the loader is waiting for a `.bin` on port 9020, send the blob **once**:

```bash
python3 tools/diag_recv.py send src/native-research/native-research.bin \
  --host <PS4_IP> --port 9020
```

`<PS4_IP>` is a placeholder: replace it with the console's IP address. **Do not probe or connect to the Bin Loader's port 9020 before the payload is ready**; the loader treats any incoming connection as an executable payload. The existing repo's `src/diag/README.md` is the authoritative hardware-loading procedure.

Expected log content **if the network receiver and console launch work**:

```
HELLO (payload executing in userland)
native research scaffold: started
extension: ENTER
extension: LEAVE
user research extension: NOT IMPLEMENTED
END
```

The receiver's actual output uses its own formatting. This build has been compiled and structurally tested **on the host only**; it has **not** been run on a PS4. It does not write to USB; the PC-side receiver writes logs to disk.

## Scope and non-goals

No direct SAMU communications, privileged memory reads, cryptographic-key recovery, secret export, or 7.55/9.00 bypass logic. The native `.bin` format and logging are independently usable for non-secret diagnostics. Keep full kernel and NOR dumps private; don't add them to this repository.
