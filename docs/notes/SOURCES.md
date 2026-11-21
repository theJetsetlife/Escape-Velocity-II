# Clean-room rebuild — source policy

Rule: everything under `rebuild/src/` is written by us, from scratch, and built by us.
The items below are **reference only** — read for ABI/offsets/structs, NEVER copied, built, or linked in.

## Reference-only (not built by us)
| Ref | Path | Used for |
|---|---|---|
| Orbis kernel (decompiles) | ~/Downloads/src (1.01/4.05 kernel, Aeolia SB fw, EAP) | FreeBSD kernel ABI, struct layouts |
| Orbis kernel (retail 9.00 dump) | /home/majorxghost/ps4debug/kernel_900_full.bin | real 9.00 offsets: icc_query_nowait +0x2E1760, sysent base +0x1100310 (stride 0x30), kbase=LSTAR-0x1c0 |
| JB WebKit | WebKitty-directboot-test/src/psfree-lapse (+ kpatch/900.c) | exploit entry ABI, sysent[11] jmp[rsi] gadget install, kpatch set |
| ps4-payload-sdk (libPS4) | ~/Documents/ps4-payload-sdk-main | userland payload API/ABI (syscall stubs, dlsym, thr) — reference, not linked |
| OpenGoldHEN | ~/Documents/OpenGoldHEN-main | payload reference only |
| strawberry Linux 7.3.3 | drive: strawberry/ | the Linux we boot (bzImage); kernel driver reference |

## Built by us (rebuild/src/)
- payload/loader: post-exploit userland -> ring0 handoff
- kexec core: kernel patch, ICC hook (+0x2E1760), 8-core rendezvous, page tables, jmp_to_target
- EDK2 integration: shim (crash-kexec target) + UPL HOB entry + round-trip
- EDK II Universal Payload: BUILT BY US from edk2 source (UefiPayloadPkg), not the prebuilt ELF
- unified build system; every output gets SHA-256 + source rev + flags recorded (book Ch 22)

## Build order (bottom-up, each must compile + be PS4-testable before the next)
0. toolchain probe + build scaffold
1. payload skeleton: crt0/entry + syscall stubs (ref payload-sdk)
2. kbase derivation + syscall(11) primitive (ref 9.00 dump / kpatch)
3. ring0: WP toggle + kernel patches + ICC hook install + rendezvous + pagetables + jmp (ref 9.00 offsets)
4. EDK2 handoff: shim entry + HOB builder + UPL entry (ref PI spec)
5. EDK II UPL built from edk2 source
