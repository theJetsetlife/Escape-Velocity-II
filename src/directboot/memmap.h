/* memmap.h - pure memory-map builder shared by the stage (hook.c) and tools/memmap_hosttest.c.
 *
 * Decides, per 2 MiB block, what EDK II may use as RAM. Everything the handoff still uses, every
 * block OrbisOS keeps out of its own allocator (phys_avail), every block that is not fully mapped
 * write-back in the kernel DMAP, and everything between TOP_MEM and 4 GiB is reserved.
 * HW history behind each rule: see the comments in memmap.c.
 */
#ifndef DB_MEMMAP_H
#define DB_MEMMAP_H
#include "../upl_load.h"

/* page attributes of one 2 MiB physical block, as the DMAP maps it */
enum mm_attr {
    MM_PG_WB      = 0,   /* every page present and write-back */
    MM_PG_NOT_WB  = 1,   /* present, some page UC/WC/WT (PCD/PWT/PAT) = device-shared */
    MM_PG_PARTIAL = 2,   /* 4K-mapped with at least one page missing */
    MM_PG_ABSENT  = 3,   /* not in the DMAP at all */
};

struct mm_range { u64 base, len; };

struct mm_ctx {
    const u64 *pav; u32 pav_n;            /* OrbisOS phys_avail pairs [start, end); pav_n 0 = no filter */
    const struct mm_range *rsv; u32 rsv_n;/* ranges the handoff still uses (region, elf, boot, fb, kblob pages) */
    u64 top_mem, tom2;                    /* AMD TOP_MEM / TOM2 */
    enum mm_attr (*attr)(void *actx, u64 pa2m);
    void *actx;
    /* high RAM the stage releases explicitly, [hi_ram_base, hi_ram_end) (0/0 = none).
     * Blocks fully inside it are RAM whatever the DMAP says (the ~5 GiB direct-memory pool is not
     * in the kernel DMAP), unless they overlap a reserved range. Set only after the stage carved
     * VRAM below it and halted the GPU engines. */
    u64 hi_ram_base, hi_ram_end;
};

struct mm_stats { u32 dropped; u64 ram_bytes; };

/* Fills in->mm[] / in->mm_count. Returns stats (dropped = blocks that did not fit the table;
 * undescribed memory is unusable to EDK II, so dropping is the safe overflow). */
struct mm_stats mm_build(const struct mm_ctx *c, struct upl_inputs *in);

/* 1 if [pa, pa+len) lies fully inside one phys_avail range */
int mm_in_pav(const struct mm_ctx *c, u64 pa, u64 len);

#endif
