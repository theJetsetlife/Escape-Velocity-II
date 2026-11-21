/* mock.h - fake PS4-like hardware for the host tests (tests/mock_hal.c). */
#ifndef PRB_MOCK_H
#define PRB_MOCK_H

#include "../core/probe.h"

struct mock_dev { u8 dev, fn; u16 vendor, device; u32 class_rev; u32 bar[6]; };

struct mock_cfg {
    int  fam16h;
    u16  kernel_ver;          /* synthesize a kernel image matching this table row (0 = no row) */
    int  break_dmap_proof;    /* corrupt PML4[PML4PML4I] */
    int  break_ident;         /* corrupt one OpenGoldHEN identity site */
    u64  lapic_base;
    struct mock_dev devs[24];
    u32  ndevs;
    u64  forbid[4][2];        /* phys ranges that must never be read (ICC, SPM) */
    u32  nforbid;
};

void mock_reset(const struct mock_cfg *m, const char *kdump);
u64  mock_kbase(void);
u64  mock_max_kern_off(void);   /* furthest kernel byte read (unknown-firmware safety) */
u32  mock_mmio_reads(void);

#endif
