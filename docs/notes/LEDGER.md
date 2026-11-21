# Evidence ledger — clean-room rebuild

Policy: "assume everything we know is wrong." A fact is **GREEN** only when re-derived in-session
from a PRIMARY artifact (raw bytes / dump decompile / exploit source), with the evidence cited.
**RED** = taken from the book/memory/prior analysis and NOT yet re-proven. No rebuild code may
depend on a RED fact. Primary artifacts:
- DUMP = retail 9.00 kernel `/home/majorxghost/ps4debug/kernel_900_full.bin` sha256 25558010…e59a58, base 0xffffffff8941c000
- LOADER = `Linux_Loader_1G_VRAM.bin` sha256 06067c68…022a5
- KPATCH = `WebKitty-directboot-test/src/psfree-lapse/kpatch/900.c` (exploit source)
- HWDUMP = `macos-ps4/dumps/fat-aeolia*/` (proc-*, acpi-tables, cpuid)

## A. Exploit → ring0 primitive (syscall 11)
| id | fact | status | evidence |
|---|---|---|---|
| A1 | sysent[11] gadget = `jmp qword ptr [rsi]` @ kbase+0x4c7ad | GREEN | DUMP raw bytes @0xffffffff894687ad = `FF 26` |
| A2 | kernel text base = 0xffffffff8941c000 | GREEN | A1 gadget lands exactly where KPATCH computes it |
| A3 | the overwritten slot is syscall #11 | GREEN | LOADER thunk `mov rax,0xb;syscall` (search_bytes) + KPATCH writes off 0x1100520 labelled syscall 11 |
| A4 | sysent base = kbase+0x1100310, stride 0x30 | RED | only KPATCH arithmetic; DUMP data seg is offset-skewed, not yet read |
| A5 | kbase = rdmsr(IA32_LSTAR) − 0x1c0 | GREEN | DUMP bytes @ kbase+0x1c0 = `0F 01 F8 65 48 89 24 25 A8 02 00 00` = swapgs;mov gs:[0x2a8],rsp = Xfast_syscall entry. So LSTAR=kbase+0x1c0. |
| A6 | DMAP base = 0xffff800000000000 \| DMPML4I<<39 \| DMPDPI<<30, with u32 globals PML4PML4I/DMPML4I/DMPDPI at kbase+0x1b904a0/a4/a8 | GREEN | DUMP: pmap code @kbase+0x12aeac and +0x12cffe does `mov eax,[rip→0x1b904a4]; mov ecx,[rip→0x1b904a8]; shl rax,0x27; shl rcx,0x1e; or` (objdump). Dump values 0x125/0x1ac/0x68 → DMAP 0xffffd61a00000000; neighbouring kernel pointers (0xffffd61a0f5aeac0…) fall inside it. Values are per-boot → payload reads them at runtime and self-checks (gate 6) |
| A7 | Launch ABI: raw blob copied into RWX MAP_ANON mapping, entered at offset 0 with no args via `chain.call_void` from the WebKit process; returns to JS | GREEN | WebKitty `lapse.mjs` runPayload() |
| A8 | After kpatch, raw `syscall` works from any user address (libkernel-origin check patched) and ring0 may execute user pages (kpatch itself runs from a user JIT mapping) | GREEN | KPATCH 900.c writes @0x490/0x4b5/0x4b9/0x4c2; lapse.mjs runs kpatch from jitshm mapping |
| A9 | Syscall numbers write 4, close 6, munmap 73, socket 97, connect 98, setsockopt 105, __sysctl 202, mlock 203, nanosleep 240, mmap 477; AF_INET 2, SOCK_STREAM 1, MAP_ANON 0x1000 | GREEN | WebKitty module/chain.mjs syscall_map + lapse.mjs constants |
| A10 | 9.00 default chain = PSFree Lapse **Bundle**; HEN slot payload is fetched, mmap'd RWX and started with `pthread_create(entry=offset 0, arg=0)` | GREEN | WebKitty firmware.js (prefs) + bundle.js PayloadLoader; Kit.zip == served tree |
| A11 | Real GoldHEN uses the same ring0 path: entry `jmp 0x16ab`; not-in-ring0 branch calls `syscall(0, 11, fn)` | GREEN | objdump of goldhen_v2.4b18.14.bin (Kit.zip original) @0x0/0x16ab; OpenGoldHEN src/loader payload_entry_check |
| A12 | Userland ABI constants (mmap/socket/sockaddr_in/timespec) | GREEN | SDK 12.508 target/include sys/mman.h, sys/socket.h, netinet/in.h, netinet/tcp.h, sys/_types/_timespec.h |
| A13 | **Phase 01 exit on hardware**: WebKitty bundle chain delivered diag.bin in the GoldHEN slot, it ran in userland, entered ring0 via syscall 11 and returned; console stayed up | GREEN | HW run 1 (reports/diag-20261119-175736): HELLO, sysctls, gates 1-5 PASS, kexec returned 0, END received |
| A14 | KASLR: kbase differs per boot (run 1: 0xffffffff88ecc000 vs dump 0xffffffff8941c000); LSTAR-0x1c0 and the 3 signature offsets hold; DMAP globals per boot (run 1: PML4PML4I 0x128, DMPML4I 0x197, DMPDPI 0x11c) | GREEN | HW run 1 gates 1-5 |
| A15 | kernel text 0xffffffff88ecc1c0 is a 2 MiB global executable leaf 0x8e001e1 (phys 0x8e00000); reading it through the DMAP alias did NOT verify (cause unknown: alias absent or differs) | GREEN (fact) / cause RED | HW run 1 gate 6; payload v2 proves DMAP via PML4 recursive slot instead and logs the alias sub-steps |
| A16 | sysctls hw.physmem/realmem/usermem, machdep.smap, vm.phys_segs/phys_free, machdep.acpi_root, hw.acpi.* do not exist on 9.00 (ENOENT); kern.osrelease "0.0-prototype", kern.version r178153 release_09.000 | GREEN | HW run 1 |
| A17 | PS4 9.00 removes kernel text from the DMAP (dmap+kernel-text phys not present) | GREEN | HW run 2 |
| H13 | Aeolia decodes config cycles for every unclaimed device number on bus 0 (aliases of 14.x appear at 00:00.1/3-7, 01.2-7, 03-17, 19-1f) | GREEN | HW run 2 PCI scan |
| H14 | CPU-side BARs under OrbisOS = Linux IOMEM (glue BAR4 0xd0200000, SPM 0xd9000000, MEM BAR2 0x80000000 1 GiB, xHCI 0xdc000000/0xdc200000/0xdc400000, GPU BAR5 0xe4800000, 14.0 BAR0 0xfdf8000000) | GREEN | HW run 2 |
| H15 | Active scanout CRTC1 1920x1080 pitch 1920 px; surface is double-buffered (run 2 0x6e800000, run 3 0x6e008000) in UC GPU-shared RAM; read-back shows opaque 32-bit pixels -> MC address == CPU phys | RED (strong inference) | HW runs 2+3; prove with an on-screen marker; fb must be read from CRTC1 at takeover |
| H16 | DRAM: TOP_MEM 0x80000000, TOM2 0x27f000000; MTRR var0 0-2 GiB WB, default UC; SYSCFG 0x760000 forces 4 GiB..TOM2 WB | GREEN | HW run 2 MSRs |
| H17 | PAT MSR 0x0001050600070406: idx6 (PAT+PCD) = WC, idx3 (PCD+PWT) = UC; OrbisOS maps graphics surfaces WC and device-shared RAM UC in the DMAP | GREEN | HW runs 2+3 |
| E3 | Takeover quiesce order (reference): LAPIC reset + MTRR per CPU, APs cpu_stop, IOMMU 0xfc000018 &= ~1, Aeolia MSI_MASK 0..7 = 0, HPET 0xd0382010/0x100-0x160 = 0, GPU soft-reset | RED (reference only) | ps4-linux-loader linux_boot.c cpu_quiesce_gate; HPET regs not yet read on HW |

## B. kernel_main kernel-patch stage (runs via A1 gadget)
| B1 | kbase derived from rdmsr(0xC0000082) | RED | loader source only (ps4-kexec = reference); re-derive our own |
| B2 | 9.00 kernel_main patches: patch1 +0x37BF3C / patch2 +0x37BF44 = 7 (kmem_alloc vm_map_insert prot args), pstate +0x29A970 = 3 (set_pstate arg) | GREEN (sites) | dump objdump: `mov $imm,%r9d` @0x37bf3a and `push $imm` @0x37bf43 inside kmem_alloc +0x37BE70; `mov $1,%edi; call 0x4d6fc0` @0x29a96f. Loader fw_offsets.h + YoRHa agree |

## C. sys_kexec (syscall 153)
| C1 | LOADER has `mov r10,rcx; mov rax,153; syscall` thunk @0x4d2ca | GREEN | search_bytes raw match |
| C2 | sys_kexec installs ICC hook + stages via kmem_alloc_contig(+0x270880)+pmap_extract(+0x12D050) | GREEN (reference) | ps4-linux-loader kexec.c/kernel.c read in full (REFERENCE_CHAIN.md); prologues verified in dump; our impl still to write |

## D. ICC shutdown takeover
| D1 | icc_query_nowait @ 0xffffffff896fd760 (+0x2E1760) | GREEN | DUMP: __func__ string xref + adjacent "ICC %02x-%02x nowait" |
| D2 | reached from shutdown_final handler @ FUN_89816c20, pri 20000 | GREEN | DUMP decompile: builds 0x7f0 ICC msg, calls icc_query_nowait |
| D3 | hookable via 5-byte E9 at its entry | GREEN | DUMP bytes @0xffffffff896fd760 = `55 48 89 E5 41 57 41 56` = push rbp;mov rbp,rsp;push r15;push r14 (straight-line prologue; hook never returns so E9 clobber is safe) |
| D4 | 9.00 kern_reboot order: shutdown_pre_sync -> "END NORMAL SHUTDOWN" -> sync disks -> shutdown_post_sync -> pstate (patched site) -> shutdown_final | GREEN | DUMP kern_reboot +0x29a380: eventhandler lookups of strings "shutdown_pre_sync"/"shutdown_post_sync"/"shutdown_final" (kern_shutdown.c) |
| D5 | shutdown_final ICC handler (+0x3fac20) prints "ICC: howto:%08x ..." and for Reboot/Shutdown/Fatal/diagOS/alt-CPU builds a 0x7f0 msg and calls icc_query_nowait @+0x3faf62; RB_HALT (howto&8) skips the call. icc_query_nowait has 3 other (runtime) callers | GREEN | DUMP disassembly + strings @0x7fc814..0x7fc890; caller scan |
| D6 | HDMI is switched off by the HDMI driver's shutdown_post_sync handler ("hdmiShutdownPostSync", via ICC) -> any takeover at shutdown_final runs on a dark screen | GREEN (name/string evidence) | DUMP strings @0xb1dc62 + "Stop HDCP HW : ICC" errors; HW runs 19:23/19:26 UTC (armed, rebooted, no signal) |

## E. CPU rendezvous / page tables / hardware teardown
| E1 | 8 CPU cores | GREEN | HW run 1 (reports/diag-20261119-175736): sysctl hw.ncpu = 8 under OrbisOS 9.00 |
| E2 | IOMMU ctrl 0xfc000018, Aeolia MSI masks, HPET, GPU reset regs | RED (partial) | IOMMU base 0xfc000000 via 00:00.2 cap, ACTIVE under OrbisOS: ctrl 0x1c0d, DevTbl 0x05318000, CmdBuf 0x7e998000, EvtLog 0x7e99c000, exclusion over whole range (HW run 2, analysis/run2/FINDINGS.md); HPET/GPU reset still RED |

## G. EDK2 handoff (the actual target)
| G1 | UPL entry ABI: jump to ELF e_entry with RCX = HOB list ptr (MS x64/EFIAPI) | GREEN | edk2 UefiPayloadEntry/UniversalPayloadEntry.c: `_ModuleEntryPoint(IN UINTN BootloaderParameter){ mHobList=(VOID*)BootloaderParameter; }` single arg; EDK2 X64 EFIAPI=ms_abi=RCX |
| G2 | required incoming HOBs | GREEN | same source BuildHobs(): PHIT handoff(56B){Ver,BootMode,MemTop/Bot,FreeTop/Bot,End} + RESOURCE_DESCRIPTOR(48B) memmap + gUniversalPayloadExtraDataGuid(REQUIRED; Entry[0].Identifier=="uefi_fv"=DXE FV) + gPcdDataBaseHobGuid + opt gUniversalPayloadAcpiTableGuid(RSDP)/gEfiGraphicsInfoHob(fb). Generic hdr {u16 type,u16 len,u32 rsvd}. PiHob.h TYPE consts 0x1..0xFFFF |
| G3 | UPL ELF = PIE entry module + FV sections | GREEN | upl-ps4-shell.elf: ELF64 DYN e_entry=0x32e6; 2x PT_LOAD(.text@0x240 RE, .data@0x8680 RW)+DYNAMIC; .rela.text/.rela.data (R_X86_64_RELATIVE); FVs in sections .upld.uefi_fv(off 0x12090) + .upld.bds_fv(off 0x3a8090); meta .upld_info |

## H. Fat/Aeolia hardware facts
| H1 | framebuffer = 1920x1080, 32bpp BGRX, stride 7680; fb_phys = RUNTIME (amdgpu-allocated) | GREEN | HWDUMP dmesg: amdgpu ps4_bridge mode "1920x1080", fbcon 240x67 (240*8=1920/67*16~1080), amdgpudrmfb 32bpp default -> stride 1920*4=7680 |
| H2a | Aeolia glue (14.4) BAR4 = CPU 0xd0200000, 2 MiB; ICC regs @BAR4+0x184000=0xd0384000 | GREEN | live IOMEM (ps4sb.icc @0xd0384000) + SRC AEOLIA_RGN_ICC_BASE 0x184000; aeolia.glue @0xd03c8000 = BAR4+0x1c8000 |
| H2b | Aeolia MSI enable/mask = MSI_MASK(func) words at BAR4+0x1c844c+4*func | GREEN | working Linux loader disables MSIs by zeroing 0xd03c844c+4i (i=0..7); ps4sb.h names them MSI_MASK; run 2 values non-zero exactly for funcs 1-5,7 (live MSIs). 0x1c8408/0x1c843c are other MSI-block regs |
| H5 | glue base values are southbridge-internal addresses, NOT CPU physical | GREEN | SRC glue_set_region bases (xHCI 0xbf400000…) vs IOMEM CPU BARs (xHCI 0xdc000000…) differ for every function |
| H6 | SPM = MEM func(14.6) BAR5 = CPU 0xd9000000, 256 KiB; ICC SPM 0xd902c000, boot params 0xd902f000 | GREEN | IOMEM spm.icc @0xd902c000 = 0xd9000000+ICC_SPM_BASE(0x2c000, SRC) |
| H7 | glue programming under OrbisOS = what SRC writes | GREEN | every window sb-aeolia.c:479-511 writes is already identical under OrbisOS; OrbisOS also sets func0, func4 bar0/4, func6 bar0/4/5, AHCI bar0 (HW run 2, analysis/run2/FINDINGS.md) |
| H8 | IOMMU MMIO 0xfc000000-0xfc07ffff; ECAM 0xf8000000-0xfbffffff; GPU MMIO 0xe4800000 | GREEN | live IOMEM |
| H9 | OrbisOS physical RAM ownership at takeover | GREEN (map) / RED (allocation) | full DMAP run map 0-16 GiB (HW run 3, analysis/run3/FINDINGS.md): kernel WB RAM ~2.4 GiB, UC/WC GPU-shared 0x58e00000-0x7fffffff + 0x100000000-0x12fffffff + 0x278000000-0x27dffffff, 0x130000000-0x273ffffff (5 GiB) not in DMAP (direct-memory pool), DMAP maps past TOM2. No free fixed range exists -> staging must be allocated via OrbisOS |
| H3 | ACPI MADT: Local APIC @0xFEE00000, 8 LAPICs (0-7), NO IOAPIC subtable | GREEN | Sony original tables at 0xE0000 (OEM SIE) read under OrbisOS; byte-identical to the HWDUMP tables Linux saw (HW run 2, analysis/run2/FINDINGS.md) |
| H4 | CPU: AMD family 16h Jaguar, 8 cores, NO AVX2, model "AMD LVPJ-B3" → btver2 | GREEN | HWDUMP cpuid leaf1 EAX=0x00710f13, 8 processors, leaf7 EBX=0x08 (AVX2 bit5 clear) |
| H10 | Glue window regs: MASK(func,bar) = BAR4+0x1c8000+func*0x30+bar*8, ADDR = MASK+4; MSI block BAR4+0x1c8400: CONTROL +0, MASK(f) +0x4c+4f, DATA_HI(f) +0x8c+4f, ADDR(f) +0xac+4f, DATA_LO +0x100.. | GREEN | SRC ps4sb.h:209-227. So 0x1c844c = MSI_MASK(func 0); 0x1c8408/0x1c843c are inside the MSI block but unnamed → H2b still needs the OrbisOS read |
| H11 | GPU register BAR = 00:01.0 BAR5 (Liverpool ≥ CHIP_BONAIRE); DCE8 dword regs GRPH_ENABLE 0x1a00, GRPH_CONTROL 0x1a01, PRIMARY_SURFACE_ADDRESS 0x1a04 / _HIGH 0x1a07, PITCH 0x1a06, X_END 0x1a0d, Y_END 0x1a0e, CRTC_CONTROL 0x1b9c; CRTC offsets 0,0x300,0x2600,0x2900,0x2c00,0x2f00; MC_VM_FB_LOCATION 0x809, MC_VM_FB_OFFSET 0x81a | GREEN | strawberry amdgpu_device.c:3916-3918, amd_asic_type.h, dce_8_0_d.h, cikd.h:39-44, gmc_7_1_d.h. Surface address is an MC address; MC→CPU phys translation is I until measured |
| H12 | ECAM base readable from the CPU: MSR C001_0058 | GREEN | 0xf8000019 = enabled, base 0xf8000000, 64 buses; MCFG (Sony original) agrees (HW run 2, analysis/run2/FINDINGS.md) |
