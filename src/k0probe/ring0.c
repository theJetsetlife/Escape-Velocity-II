/* k0probe ring0 half. Entered through syscall 11 (jmp [rsi] gadget): fn(td, uap), uap[1] = args.
 * Read-only except the idempotent kmem_alloc RWX immediates (same bytes as the working loader
 * and directboot), which are needed for a kernel copy we can execute. */
#include "k0.h"

#define PA_MASK 0x000ffffffffff000ull

typedef u64 (*kmem_alloc_t)(u64 map, u64 size);

static inline u64 rdmsr(u32 m)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m));
    return ((u64)hi << 32) | lo;
}
static inline u64 rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}
#define RDCR(n)    ({ u64 v; __asm__ volatile("mov %%cr" #n ", %0" : "=r"(v)); v; })
#define WRCR(n, v) __asm__ volatile("mov %0, %%cr" #n :: "r"((u64)(v)) : "memory")

static int beq(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}

/* Live page walk through the DMAP (as diag ring0.c, proven on HW). Level of leaf or 0. */
static int walk(u64 dmap, u64 cr3, u64 va, u64 *leaf)
{
    u64 *t = (u64 *)(dmap + (cr3 & PA_MASK));
    u64 e = t[(va >> 39) & 511];
    if (!(e & 1)) return 0;
    t = (u64 *)(dmap + (e & PA_MASK));
    e = t[(va >> 30) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) { *leaf = e; return 3; }
    t = (u64 *)(dmap + (e & PA_MASK));
    e = t[(va >> 21) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) { *leaf = e; return 2; }
    t = (u64 *)(dmap + (e & PA_MASK));
    e = t[(va >> 12) & 511];
    if (!(e & 1)) return 0;
    *leaf = e;
    return 1;
}

static int va_ok(struct k0_args *a, u64 va, u64 n)
{
    if (!n || va + n < va) return 0;
    for (u64 p = va & ~0xfffull; p < va + n; p += 0x1000) {
        u64 leaf;
        if (!walk(a->dmap, a->cr3, p, &leaf)) return 0;
    }
    return 1;
}

static u64 va_to_pa(struct k0_args *a, u64 va, u64 *leaf_out, int *lvl_out)
{
    u64 leaf = 0;
    int l = walk(a->dmap, a->cr3, va, &leaf);
    if (leaf_out) *leaf_out = leaf;
    if (lvl_out) *lvl_out = l;
    if (!l) return 0;
    u64 sz = l == 3 ? 1ull << 30 : l == 2 ? 1ull << 21 : 1ull << 12;
    return ((leaf & PA_MASK) & ~(sz - 1)) | (va & (sz - 1));
}

static void pte(struct k0_args *a, u64 va)
{
    u64 leaf; int l; va_to_pa(a, va, &leaf, &l);
    u64 lv = (u64)l;
    rec(a->log, T_PTE, va, leaf, &lv, 8);
}

/* copy a kernel-image range into one record, after a page-walk gate */
static void kread(struct k0_args *a, u64 off, u32 n, u32 id)
{
    if (off + n > K_IMAGE_SIZE || !va_ok(a, a->kbase + off, n)) {
        rec_s(a->log, T_ERR, 3 /* E_NOTMAPPED */, a->kbase + off, "kernel range not mapped - skipped");
        return;
    }
    rec(a->log, T_KMEM, off, id, (const void *)(a->kbase + off), n);
}

#define FAIL(code, msg) do { rec_s(log, T_ERR, 100 + (code), 0, msg); a->result = -(code); return 0; } while (0)

/* ---------------- step 0: bootstrap from the user mapping ---------------- */
int k0_boot(void *td, u64 *uap)
{
    (void)td;
    struct k0_args *a = (struct k0_args *)uap[1];
    if (a->magic != K0_MAGIC) return 0;
    struct diag_ctx *log = a->log;
    a->result = -1;
    a->tsc0 = rdtsc();

    static const u8 sig_xfast[12] = { 0x0f,0x01,0xf8,0x65,0x48,0x89,0x24,0x25,0xa8,0x02,0x00,0x00 };
    static const u8 sig_gadget[2] = { 0xff,0x26 };
    u64 kbase = rdmsr(0xc0000082) - K_XFAST;
    const u8 *kb = (const u8 *)kbase;
    if ((kbase & 0x3fff) || (kbase >> 32) != 0xffffffffull) FAIL(1, "kbase not plausible");
    if (!beq(kb + K_XFAST, sig_xfast, 12) || !beq(kb + K_GADGET, sig_gadget, 2))
        FAIL(2, "not retail 9.00 (xfast/gadget signature)");
    rec_s(log, T_GATE, 1, 1, "9.00 kernel identity (xfast, gadget)");

    u32 dmpml4i = *(const u32 *)(kb + K_DMPML4I), dmpdpi = *(const u32 *)(kb + K_DMPDPI);
    if (dmpml4i < 256 || dmpml4i > 511 || dmpdpi > 511) FAIL(3, "DMAP globals implausible");
    u64 dmap = 0xffff800000000000ull | (u64)dmpml4i << 39 | (u64)dmpdpi << 30;
    u64 cr3 = RDCR(3) & PA_MASK;
    u64 self = ((const u64 *)(dmap + cr3))[*(const u32 *)(kb + K_DMPML4I - 4) & 511];
    if (!(self & 1) || (self & PA_MASK) != cr3) FAIL(3, "DMAP proof failed (PML4[PML4PML4I] != CR3)");
    a->kbase = kbase; a->dmap = dmap; a->cr3 = cr3;
    rec_s(log, T_NOTE, kbase, dmap, "kbase|dmap");
    rec_s(log, T_GATE, 2, 1, "DMAP proof (self-map == CR3)");

    const u8 *p1 = kb + K_KMEM_PATCH1 - 2, *p2 = kb + K_KMEM_PATCH2 - 1;
    int ok1 = p1[0] == 0x41 && p1[1] == 0xb9 && (p1[2] == 3 || p1[2] == 7) && !p1[3] && !p1[4] && !p1[5];
    int ok2 = p2[0] == 0x6a && (p2[1] == 3 || p2[1] == 7) && p2[2] == 0xe8;
    rec_s(log, T_NOTE, (u64)p1[2] | (u64)p2[1] << 8, ok1 | ok2 << 1, "kmem_alloc prot before (p1|p2<<8)");
    if (!ok1 || !ok2) FAIL(4, "kmem_alloc patch site bytes do not match 9.00");
    u64 kernel_map = *(const u64 *)(kb + K_KERNEL_MAP);
    if ((kernel_map >> 47) != 0x1ffff) FAIL(5, "kernel_map not a kernel pointer");

    if (p1[2] != 7 || p2[1] != 7) {            /* single-byte immediates; skip when already RWX */
        u64 fl;
        __asm__ volatile("pushfq; popq %0; cli" : "=r"(fl) :: "memory");
        WRCR(0, RDCR(0) & ~(1ull << 16));
        *(volatile u8 *)(kbase + K_KMEM_PATCH1) = 7;
        *(volatile u8 *)(kbase + K_KMEM_PATCH2) = 7;
        WRCR(0, RDCR(0) | (1ull << 16));
        __asm__ volatile("pushq %0; popfq" :: "r"(fl) : "memory", "cc");
    }
    rec_s(log, T_GATE, 3, 1, "kmem_alloc maps RWX (prot 7)");

    u64 blob = (u64)(__blob_end - _start);
    u64 bsz = (blob + 0xfff) & ~0xfffull;
    u64 kblob = ((kmem_alloc_t)(kbase + K_KMEM_ALLOC))(kernel_map, bsz);
    if (!kblob || (kblob >> 47) != 0x1ffff || (kblob & 0xfff)) FAIL(6, "kmem_alloc for the kernel copy failed");
    /* NX gate: never jump into a page we cannot execute */
    for (u64 o = 0; o < bsz; o += 0x1000) {
        u64 leaf; int l = walk(dmap, cr3, kblob + o, &leaf);
        if (!l || (leaf & (1ull << 63)) || !(leaf & 2)) FAIL(7, "kernel copy page not present/RW/executable");
    }
    memcpy((void *)kblob, _start, blob);
    if (!beq((const void *)kblob, _start, (u32)blob)) FAIL(8, "kernel copy differs after memcpy");
    a->kblob = kblob; a->kblob_size = bsz;
    rec_s(log, T_NOTE, kblob, bsz, "kernel0 copy va|size");
    rec_s(log, T_GATE, 4, 1, "kernel0: probe copied into kernel memory (RWX, verified)");
    a->tsc1 = rdtsc();
    a->result = 0;
    return 0;
}

/* ---------------- steps 1..: from the kernel copy ---------------- */
int k0_step(void *td, u64 *uap)
{
    (void)td;
    struct k0_args *a = (struct k0_args *)uap[1];
    if (a->magic != K0_MAGIC) return 0;
    struct diag_ctx *log = a->log;
    a->result = -1;
    a->tsc0 = rdtsc();
    if ((u64)_start != a->kblob) FAIL(20, "step not running from the kernel copy");
    if (rdmsr(0xc0000082) - K_XFAST != a->kbase) FAIL(21, "kbase changed");

    switch (a->step) {
    case 1: {   /* where we are + AMD memory MSRs */
        u64 rip;
        __asm__ volatile("lea 0(%%rip), %0" : "=r"(rip));
        rec_s(log, T_NOTE, rip, a->kblob, "running at rip | kernel copy");
        rec_s(log, T_NOTE, va_to_pa(a, a->kblob, 0, 0), va_to_pa(a, a->kbase, 0, 0), "kernel copy pa | kernel text pa");
        rec(log, T_MSR, 0xc001001a, rdmsr(0xc001001a), 0, 0);   /* TOP_MEM */
        rec(log, T_MSR, 0xc001001d, rdmsr(0xc001001d), 0, 0);   /* TOM2 */
        rec(log, T_MSR, 0xc0010010, rdmsr(0xc0010010), 0, 0);   /* SYSCFG */
        rec(log, T_MSR, 0xc0010058, rdmsr(0xc0010058), 0, 0);   /* MMIO_CFG_BASE */
        break;
    }
    case 2:     /* getmemsize results */
        kread(a, K_PHYS_AVAIL, 0x1f0, KM_PHYS_AVAIL);
        kread(a, K_DUMP_AVAIL, 0x200, KM_DUMP_AVAIL);
        kread(a, K_PHYSMAP_COPY, 0x100, KM_PHYSMAP);
        kread(a, K_MEMVAR_A, 0x10, KM_MEMVAR_A);
        kread(a, K_MEMVAR_B, 0x30, KM_MEMVAR_B);
        kread(a, K_MEMVAR_C, 0x10, KM_MEMVAR_C);
        kread(a, K_PMAPVARS, 0x40, KM_PMAPVARS);
        break;
    case 3: {   /* vm_phys segments */
        kread(a, K_VMPHYS_HDR, 0x28, KM_VMPHYS_HDR);
        u32 n = *(const u32 *)(a->kbase + K_VMPHYS_HDR + 0x20);
        rec_s(log, T_NOTE, n, 0, "vm_phys_nsegs (as read)");
        if (n == 0 || n > 64) n = 16;            /* implausible: dump a fixed window instead */
        kread(a, K_VMPHYS_SEGS, n * 0x28, KM_VMPHYS_SEGS);
        break;
    }
    case 4: {   /* loader metadata (MODINFO records; SMAP = 0x9001) */
        u64 md = 0;
        if (!va_ok(a, a->kbase + K_PRELOAD_MD, 8)) FAIL(22, "preload_metadata variable not mapped");
        md = *(const u64 *)(a->kbase + K_PRELOAD_MD);
        rec_s(log, T_NOTE, md, md - a->kbase, "preload_metadata va | va-kbase");
        if ((md >> 47) != 0x1ffff || (md & 7)) FAIL(23, "preload_metadata not a kernel pointer");
        u64 o = 0, cap = 0x10000;
        while (o + 8 <= cap) {                   /* find the end, page-gating every header */
            if (!va_ok(a, md + o, 8)) break;
            u32 t = *(const u32 *)(md + o), s = *(const u32 *)(md + o + 4);
            if (t == 0 && s == 0) { o += 8; break; }
            if (s > cap) break;
            o += 8 + ((s + 7u) & ~7u);
        }
        if (o > cap) o = cap;
        if (!va_ok(a, md, o)) FAIL(24, "metadata range not fully mapped");
        rec(log, T_KMEM, md, KM_PRELOAD, (const void *)md, (u32)o);
        pte(a, md);
        break;
    }
    case 5:     /* live translations of the addresses above */
        pte(a, a->kbase);
        pte(a, a->kbase + K_PHYS_AVAIL);
        pte(a, a->kbase + K_VMPHYS_SEGS);
        pte(a, a->kblob);
        pte(a, (u64)a->log);
        pte(a, a->dmap);
        break;
    default:
        FAIL(25, "unknown step");
    }
    a->tsc1 = rdtsc();
    a->result = 0;
    return 0;
}
