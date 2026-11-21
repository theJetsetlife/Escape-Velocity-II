/* memmap_hosttest - checks src/directboot/memmap.c (the map EDK II gets) off the console.
 *   cc -O1 -I../src -I../src/directboot memmap_hosttest.c ../src/directboot/memmap.c -o memmap_hosttest
 *
 * Inputs mirror the PS4 Fat on 9.00: phys_avail from kernel_900_full.bin (kbase+0x2216a80,
 * k0probe README), TOP_MEM 2 GiB, TOM2 0x27f000000, the kernel-DMAP layout from run 3
 * (WB 0..0x58e00000, UC GPU-shared 0x58e00000..0x80000000, 4G..0x130000000 WB,
 * 0x130000000..0x274000000 not in the DMAP = direct-memory pool, then WB), and the handoff
 * allocations of HW run 06:08 (elf 0x800000, region 0x12e00000, boot 0x34d70000, fb = OrbisOS
 * scanout 0x6e800000, kernel-copy pages). Two bad blocks are injected (one with a missing 4K
 * page, one with a UC page) and must come out reserved. A second run forces table overflow. */
#include <stdio.h>
#include <string.h>
#include "memmap.h"

static int fails;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

#define PARTIAL_BLK 0x20000000ull     /* injected: 4K-mapped with a hole */
#define NOTWB_BLK   0x30000000ull     /* injected: one UC 4K page */

static enum mm_attr ps4_attr(void *ctx, u64 pa)
{
    (void)ctx;
    if (pa == PARTIAL_BLK) return MM_PG_PARTIAL;
    if (pa == NOTWB_BLK) return MM_PG_NOT_WB;
    if (pa < 0x58e00000ull) return MM_PG_WB;
    if (pa < 0x80000000ull) return MM_PG_NOT_WB;
    if (pa < 0x100000000ull) return MM_PG_ABSENT;          /* MMIO hole (never reached: > TOP_MEM) */
    if (pa < 0x130000000ull) return MM_PG_WB;
    if (pa < 0x274000000ull) return MM_PG_ABSENT;          /* direct-memory pool */
    return MM_PG_WB;
}

static enum mm_attr stripes_attr(void *ctx, u64 pa)      /* alternating WB / UC: worst case */
{
    (void)ctx;
    return (pa >> 21) & 1 ? MM_PG_NOT_WB : MM_PG_WB;
}

/* any RAM byte in [a, b)? */
static int ram_in(const struct upl_inputs *in, u64 a, u64 b)
{
    for (u32 i = 0; i < in->mm_count; i++) {
        const struct upl_memmap_entry *m = &in->mm[i];
        if (m->type == UPL_MM_RAM && m->base < b && m->base + m->length > a) return 1;
    }
    return 0;
}

static int sorted_disjoint(const struct upl_inputs *in)
{
    for (u32 i = 1; i < in->mm_count; i++)
        if (in->mm[i].base < in->mm[i - 1].base + in->mm[i - 1].length) return 0;
    return 1;
}

int main(void)
{
    static const u64 pav[] = {                              /* 9.00 dump, kbase+0x2216a80 */
        0x4000, 0x2c000, 0x80000, 0x98000, 0x100000, 0x600000, 0x700000, 0x9400000,
        0x14928000, 0x57738000, 0x258000000, 0x278000000, 0x27e000000, 0x27ebf0000 };
    struct mm_range rsv[] = {
        { 0x34d70000, 0x10000 },                            /* boot block */
        { 0x6e800000, 0x7e9000 },                           /* fb = OrbisOS scanout surface */
        { 0x12e00000, 0x100000 },                           /* region (1 MiB) */
        { 0x800000, 0x41eba0 },                             /* ELF copy (in-place FVs) */
        { 0x45a3f000, 0x1000 }, { 0x2b411000, 0x1000 }, { 0x7c02000, 0x1000 },   /* kernel-copy pages */
    };
    static struct upl_inputs in;
    struct mm_ctx c = { pav, sizeof pav / 16, rsv, sizeof rsv / sizeof rsv[0],
                        0x80000000ull, 0x27f000000ull, ps4_attr, 0 };

    printf("PS4 Fat 9.00 inputs:\n");
    struct mm_stats st = mm_build(&c, &in);
    CHECK(st.dropped == 0 && in.mm_count < UPL_MM_MAX, "table fits: %u entries, 0 dropped", in.mm_count);
    CHECK(sorted_disjoint(&in), "entries sorted and disjoint");
    for (u32 i = 0; i < sizeof rsv / sizeof rsv[0]; i++)
        CHECK(!ram_in(&in, rsv[i].base, rsv[i].base + rsv[i].len), "reserved range 0x%llx+0x%llx has no RAM",
              (unsigned long long)rsv[i].base, (unsigned long long)rsv[i].len);
    CHECK(!ram_in(&in, 0x57738000, 0x58e00000), "dump_avail-only hole 0x57738000-0x58e00000 has no RAM");
    CHECK(!ram_in(&in, 0x9400000, 0x14928000), "OrbisOS kernel range 0x9400000-0x14928000 has no RAM");
    CHECK(!ram_in(&in, 0x58e00000, 0x80000000), "GPU-shared UC 0x58e00000-0x80000000 has no RAM");
    CHECK(!ram_in(&in, 0x80000000, 0x100000000ull), "nothing between TOP_MEM and 4 GiB is RAM (MMIO)");
    CHECK(!ram_in(&in, 0, 0x100000), "low 1 MiB (ACPI 0xE0000, fbcon 0x8000) has no RAM");
    CHECK(!ram_in(&in, PARTIAL_BLK, PARTIAL_BLK + 0x200000), "block with a missing 4K page is reserved");
    CHECK(!ram_in(&in, NOTWB_BLK, NOTWB_BLK + 0x200000), "block with a UC 4K page is reserved");
    CHECK(ram_in(&in, 0x14a00000, 0x14c00000) && ram_in(&in, 0x40000000, 0x40200000), "phys_avail WB blocks are RAM");
    CHECK(!ram_in(&in, 0x100000000ull, 0x130000000ull), "4G..0x130000000 (WB, not in phys_avail) reserved");
    CHECK(!ram_in(&in, 0x130000000ull, 0x274000000ull), "direct-memory pool (not in DMAP, > 4 GiB) reserved");
    CHECK(st.ram_bytes > (1100ull << 20), "RAM total %.1f MiB below+above 4 GiB", st.ram_bytes / 1048576.0);
    u64 below = 0;
    for (u32 i = 0; i < in.mm_count; i++)
        if (in.mm[i].type == UPL_MM_RAM && in.mm[i].base < (1ull << 32)) below += in.mm[i].length;
    CHECK(below >= (512ull << 20), "RAM below 4 GiB %.1f MiB (UPL needs a 12 MiB block below 4G)", below / 1048576.0);

    printf("overflow (alternating WB/UC blocks, no phys_avail filter):\n");
    struct mm_ctx o = { 0, 0, 0, 0, 0x80000000ull, 0x100000000ull, stripes_attr, 0 };
    st = mm_build(&o, &in);
    CHECK(in.mm_count == UPL_MM_MAX && st.dropped > 0, "table full (%u), %u blocks dropped, not stretched", in.mm_count, st.dropped);
    CHECK(sorted_disjoint(&in), "entries still sorted and disjoint");
    u32 bad = 0;
    for (u32 i = 0; i < in.mm_count; i++) if (in.mm[i].length > 0x200000 && in.mm[i].base >= 0x200000) bad++;
    CHECK(bad == 0, "no entry spans more than one 2 MiB stripe (no gap stretching)");
    CHECK(!ram_in(&in, 0x80000000, 0x100000000ull), "no RAM in the MMIO hole after overflow");

    printf("2 GiB VRAM carve at 4 GiB, high RAM released 0x180000000-0x274000000:\n");
    struct mm_range rsv2[sizeof rsv / sizeof rsv[0] + 1];
    memcpy(rsv2, rsv, sizeof rsv);
    rsv2[sizeof rsv / sizeof rsv[0]] = (struct mm_range){ 0x100000000ull, 0x80000000ull };   /* VRAM carve */
    struct mm_ctx h = c;
    h.rsv = rsv2; h.rsv_n = sizeof rsv2 / sizeof rsv2[0];
    h.hi_ram_base = 0x180000000ull; h.hi_ram_end = 0x274000000ull;
    st = mm_build(&h, &in);
    CHECK(st.dropped == 0 && sorted_disjoint(&in), "table fits (%u entries), sorted", in.mm_count);
    CHECK(!ram_in(&in, 0x100000000ull, 0x180000000ull), "VRAM carve 0x100000000-0x17fffffff has no RAM");
    CHECK(ram_in(&in, 0x180000000ull, 0x180200000ull) && ram_in(&in, 0x273e00000ull, 0x274000000ull), "released pool is RAM end to end");
    CHECK(!ram_in(&in, 0x278000000ull, 0x27e000000ull), "device-shared UC 0x278000000-0x27dffffff reserved");
    CHECK(!ram_in(&in, 0x80000000, 0x100000000ull) && !ram_in(&in, 0x27f000000ull, 0x400000000ull), "MMIO hole and above TOM2 stay non-RAM");
    for (u32 i = 0; i < sizeof rsv / sizeof rsv[0]; i++)
        CHECK(!ram_in(&in, rsv[i].base, rsv[i].base + rsv[i].len), "handoff range 0x%llx still reserved", (unsigned long long)rsv[i].base);
    CHECK(st.ram_bytes > (5000ull << 20), "RAM total %.1f MiB (~6 GiB system RAM target)", st.ram_bytes / 1048576.0);
    { struct mm_range hr = { 0x200000000ull, 0x1000 }; struct mm_range r3[sizeof rsv2 / sizeof rsv2[0] + 1];
      memcpy(r3, rsv2, sizeof rsv2); r3[sizeof rsv2 / sizeof rsv2[0]] = hr; h.rsv = r3; h.rsv_n++;
      st = mm_build(&h, &in);
      CHECK(!ram_in(&in, 0x200000000ull, 0x200001000ull), "reserved page inside the released pool wins (e.g. kblob page)"); }

    printf("fail closed: no phys_avail, no hi_ram:\n");
    struct mm_ctx f = c; f.pav_n = 0;
    st = mm_build(&f, &in);
    CHECK(!ram_in(&in, 0x130000000ull, 0x274000000ull), "pool NOT RAM without phys_avail (was fail-open)");

    printf("RESULT: %s\n", fails ? "FAILED" : "all checks passed");
    return fails != 0;
}
