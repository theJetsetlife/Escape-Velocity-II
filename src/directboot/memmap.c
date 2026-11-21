/* memmap.c - see memmap.h. Freestanding (stage) and host-compilable (tools/memmap_hosttest.c). */
#include "memmap.h"

#define BLK (2ull << 20)

static int overlaps(u64 pa, u64 len, u64 base, u64 blen)
{
    return blen && len && pa + len > base && pa < base + blen;
}

int mm_in_pav(const struct mm_ctx *c, u64 pa, u64 len)
{
    for (u32 i = 0; i < c->pav_n; i++)
        if (pa >= c->pav[2 * i] && pa + len <= c->pav[2 * i + 1]) return 1;
    return 0;
}

/* Append, merging with the previous entry when contiguous and of the same type. When the table is
 * full the entry is DROPPED (code review 2026-11-20: the old version stretched the last entry over
 * any gap and turned it RESERVED, which could cover the MMIO hole and lose all RAM above 4 GiB). */
static void mm_add(struct upl_inputs *in, struct mm_stats *st, u64 base, u64 len, u32 type)
{
    if (!len) return;
    if (in->mm_count) {
        struct upl_memmap_entry *l = &in->mm[in->mm_count - 1];
        if (l->type == type && l->base + l->length == base) { l->length += len; goto count; }
    }
    if (in->mm_count == UPL_MM_MAX) { st->dropped++; return; }
    in->mm[in->mm_count].base = base; in->mm[in->mm_count].length = len;
    in->mm[in->mm_count].type = type; in->mm_count++;
count:
    if (type == UPL_MM_RAM) st->ram_bytes += len;
}

static u32 block_type(const struct mm_ctx *c, u64 pa)
{
    /* Memory the handoff still uses (HW 23:53/00:30 UTC: DXE allocations landed on it). */
    for (u32 i = 0; i < c->rsv_n; i++)
        if (overlaps(pa, BLK, c->rsv[i].base, c->rsv[i].len)) return UPL_MM_RESERVED;
    if (c->hi_ram_end > c->hi_ram_base && pa >= c->hi_ram_base && pa + BLK <= c->hi_ram_end)
        return UPL_MM_RAM;                               /* released high RAM */
    switch (c->attr(c->actx, pa)) {
    case MM_PG_ABSENT:
        /* not in the kernel DMAP. Below 4 GiB: kernel text / small carve-outs (possibly SAMU).
         * Above 4 GiB: the ~5 GiB direct-memory pool (run 3) = game/GPU memory OrbisOS hands to
         * apps, not its VM; devices may still target it, and EDK II allocates top-down, so it
         * would land there first. With a valid phys_avail, reserved (same rule as below); the
         * fail CLOSED (the old no-phys_avail fallback made the whole pool RAM); the pool
         * is RAM only through the explicit hi_ram range above. */
        return UPL_MM_RESERVED;
    case MM_PG_NOT_WB:   /* UC/WC = device-shared (run 3) */
    case MM_PG_PARTIAL:  /* code review: a hole inside the block may be a firmware page */
        return UPL_MM_RESERVED;
    case MM_PG_WB:
        break;
    }
    /* DMAP-mapped RAM that OrbisOS keeps out of phys_avail is firmware/hardware owned
     * (HW 23:53/00:30/01:50 UTC; 0x57738000-0x58e00000 in the 9.00 dump). */
    if (c->pav_n && !mm_in_pav(c, pa, BLK)) return UPL_MM_RESERVED;
    return UPL_MM_RAM;
}

struct mm_stats mm_build(const struct mm_ctx *c, struct upl_inputs *in)
{
    struct mm_stats st = { 0, 0 };
    in->mm_count = 0;
    /* low 1 MiB: fbcon 0x8000, Sony ACPI at 0xE0000. 0xA0000-0xBFFFF (legacy VGA window) stays a hole:
     * PCI root-bridge _CRS claims it, and a memory-map entry over it stops Windows with
     * ACPI_BIOS_ERROR 0x2 (root resources; QEMU 2026-11-20). */
    mm_add(in, &st, 0, 0xA0000, UPL_MM_RESERVED);
    mm_add(in, &st, 0xC0000, 0x40000, UPL_MM_RESERVED);
    mm_add(in, &st, 0x100000, 0x100000, block_type(c, 0) == UPL_MM_RAM ? UPL_MM_RAM : UPL_MM_RESERVED);
    for (u64 pa = BLK; pa + BLK <= c->top_mem; pa += BLK)
        mm_add(in, &st, pa, BLK, block_type(c, pa));
    for (u64 pa = 1ull << 32; pa < c->tom2; pa += BLK) {
        u64 len = pa + BLK <= c->tom2 ? BLK : c->tom2 - pa;
        mm_add(in, &st, pa, len, len == BLK ? block_type(c, pa) : UPL_MM_RESERVED);
    }
    return st;
}
