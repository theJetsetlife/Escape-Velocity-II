# Building

## Prerequisites (Linux x86-64)
- Stage: `clang`, `ld.lld`, `llvm-objcopy`, `make`
- EDK II: `gcc`, `g++`, `make`, `nasm`, `iasl` (acpica), `python3`, `uuid` headers (`uuid-dev` / `libuuid`), `git`
- Tests: `cc`, `qemu-system-x86_64` (KVM), `mtools`, `ntfs-3g` (`mkntfs`, `ntfscp`), `dosfstools`
- Windows tests (optional): `7z`, `sgdisk`
- Hardware runs: `docker` (compose v2), `curl`

## Steps
```sh
make setup        # scripts/setup-edk2.sh: clone tianocore/edk2, checkout 2970e569, submodules, BaseTools
make upl          # Ps4Pkg/build.sh -> out/upl-ps4.elf (+ .sha256, out/upl-ps4-build.log)
make stage        # src/directboot -> directboot.bin, PC address from server/.env (PC_IP, PC_PORT)
make dsdt         # only after editing Ps4Pkg/Ps4AcpiDxe/Ps4Dsdt.asl: regenerates Ps4Dsdt.h (needs iasl)
```
- `EDK2_URL=/path/to/local/edk2 make setup` clones from a local mirror instead of GitHub.
- `EDK2=/other/edk2 make upl` builds against another checkout (it must be the same commit: `mkplatform.py`
  applies exact-match edits to upstream UefiPayloadPkg and stops if they do not match).
- edk2 stays unmodified (`setup-edk2.sh` refuses a dirty tree). Every PS4 change is in `Ps4Pkg/`.
- Build flags (one logical CPU, LAPIC timer, network on, TLS/IPv6 off, LAPIC clock 99.604 MHz, 12 MiB first
  UEFI region) are in `Ps4Pkg/build.sh`; each has a reason in the comments there.
- The UPL build is reproducible for a given commit (`SOURCE_DATE_EPOCH` from git).

## Stage build gates
`src/directboot/Makefile` fails the build if the blob has absolute/GOT relocations, dynamic sections or SSE
instructions (it runs from two addresses — the user mapping and its kernel copy — in kernel context without an
FPU save). The PC address lives in a 16-byte config block at offset 0x10 (`DBBOOTCF`); a built stage can be
retargeted without rebuilding: `python3 tools/diag_recv.py patch directboot.bin --ip A.B.C.D --port N`.
