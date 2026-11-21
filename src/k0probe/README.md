# k0probe — kernel-0 RAM-address probe for OrbisOS 9.00

Read-only payload that pulls OrbisOS's own physical-memory bookkeeping, so directboot can
stage/reserve from measured addresses instead of guesses. Nothing is armed or hooked, and the
console isn't rebooted: OrbisOS keeps running and the probe can be re-run in the same session.

## kernel-0 pattern
| step | runs from | does |
|---|---|---|
| 0 | WebKit user mapping | 9.00 identity gates, DMAP proof, kmem_alloc RWX patch (idempotent; the only write), kmem_alloc kernel copy, NX/RW gate on every page, copy + compare |
| 1 | **kernel copy** | proves rip == copy; copy/text phys; TOP_MEM, TOM2, SYSCFG, MMIO_CFG_BASE |
| 2 | kernel copy | phys_avail[], dump_avail[], hammer_time physmap copy, memvars |
| 3 | kernel copy | vm_phys_nsegs + vm_phys_segs[] |
| 4 | kernel copy | preload_metadata walk -> loader SMAP (MODINFOMD_SMAP 0x9001) |
| 5 | kernel copy | live PTEs of kbase, data, kernel copy, log buffer, DMAP |

Each step is one syscall 11 and is flushed to the PC before the next starts, so a crash pins the
step. Every kernel VA is page-walked before it is read.

## 9.00 offsets (from kernel_900_full.bin, B = byte-confirmed in the dump)
| symbol | kbase+ | evidence |
|---|---|---|
| phys_avail[] | 0x2216a80 | lea in hammer_time/getmemsize @+0x314262; dump 4000-2c000 ... 27e000000-27ebf0000 |
| dump_avail[] | 0x2216c70 | lea @+0x31425b; dump ... 700000-58e00000, 258000000-278000000, 27e000000-27ec00000 |
| physmap copy | 0x21eef40 | header 1,0,a00000000,14928000 + pairs |
| hw.physmem result | 0x21fa1f8 | stores after getenv "hw.physmem" @+0x313fb8; dump 0x9fb00 |
| vm_phys_nsegs / segs | 0x22d3b68 / 0x22d3b70 | dump: 8 segs {start,end,first_page,domain,free_queues}, stride 0x28 |
| preload_metadata | 0x21ef6c8 | read by preload_search_by_type @+0x27a8b0; dump value kbase+0x2822000 (past the dump: live only) |
| preload_search_info | 0x27a850 | getmemsize calls it with 0x9001 (SMAP) |

## Run
1. PC: receiver + WebKitty running (`webkitty-docker/run.sh` stack; already up). `k0probe.bin` is
   in the GoldHEN slot (`WebKitty-directboot-test/includes/payloads/HEN/goldhen_v*.bin`).
2. PS4: reload the WebKitty page once (appcache), GoldHEN, jailbreak.
3. PC: `python3 -I tools/k0probe_decode.py reports/diag-<stamp>.bin --json analysis/k0probe/<stamp>.json`

Restore the boot stage: `cp src/directboot/directboot.bin` over each `goldhen_v*.bin` (e86680d9).
