/* mock_hal.c - host implementation of core/hal.h over fake hardware.
 * Any read the probe should not make aborts the test: an MSR outside the scenario's set (#GP on
 * real hardware), physical memory outside the fake regions, a forbidden range (ICC/SPM), a kernel
 * byte outside the image. Page tables, MTRRs, PAT, ECAM and ACPI are real data structures, so the
 * probe's own gates are exercised, not bypassed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock.h"
#include "../core/hal.h"
#include "fwsel.h"
#include "../firmware/fw.h"
#include "fwident.h"

#define KBASE   0xffffffff8941c000ull
#define KSIZE   0x3000000ull
#define ECAM    0xf8000000ull

struct region { u64 pa, n; u8 *buf; };
static struct region R[16];
static u32 nr;
static const struct mock_cfg *M;
static u8 *kimg;
static u64 kmax_off, mmio_reads, tsc;

static void die(const char *what, u64 v)
{
    fprintf(stderr, "MOCK VIOLATION: %s 0x%llx\n", what, (unsigned long long)v);
    exit(2);
}

static u8 *region_add(u64 pa, u64 n)
{
    R[nr].pa = pa; R[nr].n = n; R[nr].buf = calloc(1, n);
    return R[nr++].buf;
}

static void *phys(u64 pa, u64 n)
{
    for (u32 i = 0; i < M->nforbid; i++)
        if (pa < M->forbid[i][0] + M->forbid[i][1] && pa + n > M->forbid[i][0]) die("read of a FORBIDDEN range", pa);
    for (u32 i = 0; i < nr; i++)
        if (pa >= R[i].pa && pa + n <= R[i].pa + R[i].n) return R[i].buf + (pa - R[i].pa);
    die("read of physical memory outside the fake machine", pa);
    return 0;
}

static void w32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static void w64(u8 *p, u64 v) { memcpy(p, &v, 8); }
static void csum(u8 *t, u32 len, u32 at) { u8 s = 0; t[at] = 0; for (u32 i = 0; i < len; i++) s += t[i]; t[at] = (u8)-s; }

static void acpi_hdr(u8 *t, const char *sig, u32 len) { memcpy(t, sig, 4); w32(t + 4, len); t[8] = 2; memcpy(t + 10, "SIE   ", 6); }

static void build_kernel(u16 ver, const char *kdump)
{
    kimg = calloc(1, KSIZE);
    static const u8 xfast[12] = { 0x0f, 0x01, 0xf8, 0x65, 0x48, 0x89, 0x24, 0x25, 0xa8, 0x02, 0x00, 0x00 };
    const struct db_kofs *k = ver ? fw_row_for(ver) : 0;
    if (kdump && ver == 900) {
        FILE *f = fopen(kdump, "rb");
        if (!f) die("cannot open KDUMP", 0);
        size_t got = fread(kimg, 1, KSIZE, f); fclose(f);
        (void)got;
    } else {
        memcpy(kimg + FW_K_XFAST, xfast, sizeof xfast);
        if (k) {
            static const u8 pro[4] = { 0x55, 0x48, 0x89, 0xe5 };
            static const u8 icc[16] = { 0x55, 0x48,0x89,0xe5, 0x41,0x57, 0x41,0x56, 0x41,0x54, 0x53, 0x80,0x7f,0x01,0x04, 0x90 };
            const u32 fns[] = { k->kmem_alloc, k->kmem_contig, k->pmap_extract, k->smp_rdv, k->smp_nobarrier, k->wlanbt, k->kern_reboot };
            for (u32 i = 0; i < 7; i++) memcpy(kimg + fns[i], pro, 4);
            memcpy(kimg + k->icc_nowait, icc, 16);
            if (k->gadget) { kimg[k->gadget] = 0xff; kimg[k->gadget + 1] = 0x26; }
            static const u8 p1[6] = { 0x41, 0xb9, 3, 0, 0, 0 }, p2[3] = { 0x6a, 3, 0xe8 }, ps[6] = { 0xbf, 1, 0, 0, 0, 0xe8 };
            memcpy(kimg + k->kmem_patch1 - 2, p1, 6);
            memcpy(kimg + k->kmem_patch2 - 1, p2, 3);
            memcpy(kimg + k->pstate - 1, ps, 6);
            w32(kimg + k->pml4pml4i, 256); w32(kimg + k->dmpml4i, 257); w32(kimg + k->dmpdpi, 0);
            w64(kimg + k->kernel_map, 0xffffff8000123000ull);
        }
        for (u32 i = 0; i < FW_IDENT_COUNT; i++) {            /* OpenGoldHEN identity sites */
            const struct fw_ident *r = &fw_ident_table[i];
            if (r->ver != ver) continue;
            static const u8 pro[4] = { 0x55, 0x48, 0x89, 0xe5 };
            for (u32 s = 0; s < 8; s++) if (r->site[s]) memcpy(kimg + r->site[s], pro, 4);
            kimg[r->kexec_rop] = 0xff; kimg[r->kexec_rop + 1] = 0x26;
            w64(kimg + r->kernel_map, 0xffffff8000123000ull);
        }
    }
}

void mock_reset(const struct mock_cfg *m, const char *kdump)
{
    for (u32 i = 0; i < nr; i++) free(R[i].buf);
    nr = 0; M = m; kmax_off = 0; mmio_reads = 0; tsc = 1000;
    free(kimg);
    build_kernel(m->kernel_ver, kdump);
    if (m->break_ident)
        for (u32 i = 0; i < FW_IDENT_COUNT; i++)
            if (fw_ident_table[i].ver == m->kernel_ver) kimg[fw_ident_table[i].site[0]] = 0xcc;

    /* page tables (DMAP indices taken from the kernel image when a row exists, as the probe does) */
    u32 pml4i = 256, dml4 = 257, ddpi = 0;
    const struct db_kofs *k = m->kernel_ver ? fw_row_for(m->kernel_ver) : 0;
    if (k) { memcpy(&pml4i, kimg + k->pml4pml4i, 4); memcpy(&dml4, kimg + k->dmpml4i, 4); memcpy(&ddpi, kimg + k->dmpdpi, 4); }
    u8 *low = region_add(0, 0x10000);
    u8 *pml4 = low + 0x1000, *pdpt = low + 0x2000;
    w64(pml4 + 8 * (pml4i & 511), m->break_dmap_proof ? 0x5000 | 1 : 0x1000 | 3);
    w64(pml4 + 8 * (dml4 & 511), 0x2000 | 3);
    for (u64 g = 0; g < 16; g++)                       /* 1 GiB leaves; GiB 3 (MMIO) PCD|PWT -> PAT 3 = UC */
        w64(pdpt + 8 * ((g + ddpi) & 511), g << 30 | 0x83 | (g == 3 ? 0x18 : 0));

    /* ACPI: RSDP at 0xE0000 -> XSDT -> FACP(->DSDT), APIC */
    u8 *bios = region_add(0xe0000, 0x20000);
    u8 *tab = region_add(0x7f000000, 0x10000);
    memcpy(bios, "RSD PTR ", 8); bios[15] = 2; w32(bios + 16, 0); w64(bios + 24, 0x7f000000); w32(bios + 20, 36);
    csum(bios, 20, 8);
    acpi_hdr(tab, "XSDT", 36 + 16); w64(tab + 36, 0x7f001000); w64(tab + 44, 0x7f002000); csum(tab, 52, 9);
    u8 *facp = tab + 0x1000; acpi_hdr(facp, "FACP", 244); w32(facp + 40, 0x7f003000); csum(facp, 244, 9);
    u8 *apic = tab + 0x2000; acpi_hdr(apic, "APIC", 44 + 8); w32(apic + 36, 0xfee00000);
    apic[44] = 0; apic[45] = 8; apic[46] = 0; apic[47] = 0; w32(apic + 48, 1); csum(apic, 52, 9);
    u8 *dsdt = tab + 0x3000; acpi_hdr(dsdt, "DSDT", 36); csum(dsdt, 36, 9);

    /* ECAM bus 0 and every device BAR window */
    u8 *ecam = region_add(ECAM, 1u << 20);
    for (u32 i = 0; i < m->ndevs; i++) {
        const struct mock_dev *d = &m->devs[i];
        u8 *cfg = ecam + ((u32)d->dev << 15 | (u32)d->fn << 12);
        w32(cfg, (u32)d->device << 16 | d->vendor);
        w32(cfg + 8, d->class_rev);
        if (d->fn == 0) cfg[0xe] = 0x80;
        for (u32 b = 0; b < 6; b++) if (d->bar[b]) { w32(cfg + 0x10 + 4 * b, d->bar[b]); region_add(d->bar[b], 0x200000); }
    }
    if (m->lapic_base) region_add(m->lapic_base, 0x1000);
}

u64 mock_kbase(void) { return KBASE; }
u64 mock_max_kern_off(void) { return kmax_off; }
u32 mock_mmio_reads(void) { return (u32)mmio_reads; }

/* ---- core/hal.h --------------------------------------------------------- */
u64 hal_rdmsr(u32 m)
{
    switch (m) {
    case 0xc0000082: return KBASE + FW_K_XFAST;
    case 0xc0000080: return 0xd01;
    }
    if (!M->fam16h) die("rdmsr of a model-specific MSR on a non-family-16h CPU", m);
    switch (m) {
    case 0x1b: return M->lapic_base | 0x900;
    case 0xfe: return 0x508;                                      /* 8 variable MTRRs */
    case 0x2ff: return 0xc06;                                     /* enabled, default WB */
    case 0x200: return 0xc0000000ull;                             /* UC 0xC0000000-0xFFFFFFFF */
    case 0x201: return (~0x3fffffffull & 0xffffffffffull) | 0x800;
    case 0x277: return 0x0007040600070406ull;
    case 0xc0010010: case 0xc001001d: case 0xc0010015: case 0xc001001a: case 0xc0000081: case 0xc0010114: return 0;
    case 0xc0010058: return ECAM | (8u << 2) | 1;                 /* ECAM enabled, 256 buses */
    }
    if ((m >= 0x202 && m <= 0x20f) || m == 0x250 || m == 0x258 || m == 0x259 || (m >= 0x268 && m <= 0x26f)) return 0;
    die("rdmsr of an MSR this core does not implement (#GP)", m);
    return 0;
}

u64 hal_rdcr(int n) { return n == 0 ? 0x80050033ull : n == 3 ? 0x1000 : 0x6e0; }
u64 hal_rflags(void) { return 0x46; }
u64 hal_rdtsc(void) { return tsc += 1593719000ull / 5; }

void hal_cpuid(u32 leaf, u32 sub, u32 r[4])
{
    (void)sub;
    memset(r, 0, 16);
    if (M->fam16h) {
        if (leaf == 0) { r[0] = 0xd; r[1] = 0x68747541; r[2] = 0x444d4163; r[3] = 0x69746e65; }   /* AuthenticAMD */
        if (leaf == 1) { r[0] = 0x00730f01; r[1] = 0x00080800; }
        if (leaf == 7) r[1] = 0x08;
        if (leaf == 0x80000000) r[0] = 0x8000001b;
        if (leaf == 0x80000008) { r[0] = 0x3028; r[2] = 0x7; }
    } else {
        if (leaf == 0) r[0] = 0x16;
        if (leaf == 1) r[0] = 0x000906ea;
        if (leaf == 0x80000000) r[0] = 0x80000008;
        if (leaf == 0x80000008) r[0] = 0x3027;
    }
}

void *hal_phys(const struct prb_ctx *c, u64 pa, u64 n)
{
    if (!c->dmap) die("physical access without a proven DMAP", pa);
    return phys(pa, n);
}

u32 hal_mmio32(const struct prb_ctx *c, u64 pa)
{
    if (!c->dmap) die("MMIO without a proven DMAP", pa);
    mmio_reads++;
    u32 v; memcpy(&v, phys(pa, 4), 4);
    return v;
}

const u8 *hal_kern(u64 va)
{
    if (va < KBASE || va >= KBASE + KSIZE) die("kernel read outside the image", va);
    if (va - KBASE > kmax_off) kmax_off = va - KBASE;
    return kimg + (va - KBASE);
}
