/* hal.h - the only way diagnostics touch hardware. Read primitives only: there is no write
 * function in this interface, and `make check` fails if the payload disassembly contains wrmsr,
 * out/outs, or a move to a control register.
 *   payload build : inline instructions, physical memory through the proven DMAP
 *   PROBE_HOST    : tests/mock_hal.c (fake CPU, page tables, ECAM, MMIO with access logging)
 */
#ifndef PRB_HAL_H
#define PRB_HAL_H

#include "probe.h"

#ifdef PROBE_HOST
u64 hal_rdmsr(u32 m);
u64 hal_rdcr(int n);
u64 hal_rflags(void);
u64 hal_rdtsc(void);
void hal_cpuid(u32 leaf, u32 sub, u32 r[4]);
void *hal_phys(const struct prb_ctx *c, u64 pa, u64 n);     /* RAM, after ram_ok() */
u32 hal_mmio32(const struct prb_ctx *c, u64 pa);            /* MMIO, after mmio_ok() */
const u8 *hal_kern(u64 va);                                  /* kernel image byte */
#define hal_irq_save()     0ull
#define hal_irq_restore(f) ((void)(f))
#else
static inline u64 hal_rdmsr(u32 m)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m));
    return ((u64)hi << 32) | lo;
}
static inline u64 hal_rdcr(int n)
{
    u64 v = 0;
    if (n == 0) __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    else if (n == 3) __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    else if (n == 4) __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}
static inline u64 hal_rflags(void) { u64 f; __asm__ volatile("pushfq; popq %0" : "=r"(f)); return f; }
static inline u64 hal_rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}
static inline void hal_cpuid(u32 leaf, u32 sub, u32 r[4])
{
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}
static inline void *hal_phys(const struct prb_ctx *c, u64 pa, u64 n) { (void)n; return (void *)(c->dmap + pa); }
static inline u32 hal_mmio32(const struct prb_ctx *c, u64 pa) { return *(volatile u32 *)(c->dmap + pa); }
static inline const u8 *hal_kern(u64 va) { return (const u8 *)va; }
static inline u64 hal_irq_save(void) { u64 f; __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory"); return f; }
static inline void hal_irq_restore(u64 f) { __asm__ volatile("pushq %0; popfq" :: "r"(f) : "memory", "cc"); }
#endif

/* core/gates.c: nothing is read before one of these passes */
int  prb_walk(const struct prb_ctx *c, u64 va, u64 *leaf);   /* leaf level 1/2/3, 0 = absent */
int  prb_ram_ok(const struct prb_ctx *c, u64 pa, u64 n);     /* present in the DMAP          */
int  prb_mmio_ok(struct prb_ctx *c, u64 pa, u64 n);          /* present + effectively UC     */
u32  prb_mtrr_type(const struct prb_ctx *c, u64 pa);
u32  prb_pat_type(u64 leaf, int lvl);
void prb_dump_mmio(struct prb_ctx *c, u64 pa, u32 bytes, u64 region);
u32  prb_cfg32(const struct prb_ctx *c, u32 bus, u32 dev, u32 fn, u32 off);
u64  prb_bar(const struct prb_ctx *c, u32 bus, u32 dev, u32 fn, u32 off);
int  prb_memeq(const void *a, const void *b, u32 n);

#endif
