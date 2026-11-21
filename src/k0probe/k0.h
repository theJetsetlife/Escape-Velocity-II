/* k0probe — kernel-0 memory probe for OrbisOS 9.00 (read-only).
 *
 * Pattern ("kernel0", same as ps4-linux-loader kernel_main -> kexec blob):
 *   step 0  (runs from the WebKit user mapping): identity gates, DMAP proof, kmem_alloc RWX
 *           patch (idempotent, the only write), kmem_alloc a kernel copy, NX gate, copy blob.
 *   step 1+ syscall 11 straight to the KERNEL COPY; each step checks it is running there.
 *           Every kernel VA is page-walked before it is read. Nothing else is written.
 *
 * What it pulls (OrbisOS's own RAM bookkeeping, offsets from the 9.00 dump, see README):
 *   phys_avail[], dump_avail[], hammer_time physmap copy, vm_phys_segs[], preload metadata
 *   (loader SMAP = MODINFOMD_SMAP 0x9001), TOP_MEM/TOM2/SYSCFG, live PTEs of key VAs.
 */
#ifndef K0_H
#define K0_H

#include "../diag/rec.h"

/* ---- 9.00 kernel offsets: from kernel_900_full.bin (sha 25558010...) ---- */
#define K_XFAST         0x1c0        /* LEDGER A5 */
#define K_GADGET        0x4c7ad      /* LEDGER A1 */
#define K_DMPML4I       0x1b904a4    /* LEDGER A6 */
#define K_DMPDPI        0x1b904a8
#define K_KMEM_ALLOC    0x37be70     /* LEDGER B2 */
#define K_KMEM_PATCH1   0x37bf3c
#define K_KMEM_PATCH2   0x37bf44
#define K_KERNEL_MAP    0x2268d48
/* new (k0probe), read out of hammer_time/getmemsize @+0x313c60 and the dump's data */
#define K_PHYS_AVAIL    0x2216a80    /* lea @+0x314262/+0x314368; dump: 4000,2c000,80000,... */
#define K_DUMP_AVAIL    0x2216c70    /* lea @+0x31425b; dump: 0,2c000,...,27ec00000 */
#define K_PHYSMAP_COPY  0x21eef40    /* dump: ...,80000,9a000,...,700000,58e00000,... */
#define K_MEMVAR_A      0x21fa1f8    /* hw.physmem result in getmemsize (Maxmem?) */
#define K_MEMVAR_B      0x21fb250    /* written in hammer_time (+0x3140f5, +0x313efe) */
#define K_MEMVAR_C      0x2216770    /* written at +0x3145bb */
#define K_PMAPVARS      0x1b90480    /* block before the DMAP globals */
#define K_VMPHYS_HDR    0x22d3b48    /* +0x20 = vm_phys_nsegs (dump: 8) */
#define K_VMPHYS_SEGS   0x22d3b70    /* struct vm_phys_seg, stride 0x28 */
#define K_PRELOAD_MD    0x21ef6c8    /* preload_metadata (read by preload_search_by_type @+0x27a8b0) */
#define K_IMAGE_SIZE    0x2820230    /* size of the dump = kernel image span we trust */

#define T_KMEM          16           /* a = kernel offset (or VA for metadata), b = id; data raw */
enum k0_id {
    KM_PHYS_AVAIL = 1, KM_DUMP_AVAIL = 2, KM_PHYSMAP = 3, KM_MEMVAR_A = 4, KM_MEMVAR_B = 5,
    KM_MEMVAR_C = 6, KM_PMAPVARS = 7, KM_VMPHYS_HDR = 8, KM_VMPHYS_SEGS = 9, KM_PRELOAD = 10,
};

#define K0_MAGIC   0x45424f5250304b44ull   /* "DK0PROBE" */
#define K0_STEPS   6

struct k0_args {
    u64 magic;
    struct diag_ctx *log;
    u32 step;
    int result;                /* 0 ok, <0 = failing check */
    u64 kbase, dmap, cr3;
    u64 kblob, kblob_size;
    u64 tsc0, tsc1;             /* TSC around the step (ring0) */
};

extern char _start[], __blob_end[];
int k0_boot(void *td, u64 *uap);   /* step 0, from the user mapping */
int k0_step(void *td, u64 *uap);   /* steps 1..K0_STEPS-1, from the kernel copy */

#endif
