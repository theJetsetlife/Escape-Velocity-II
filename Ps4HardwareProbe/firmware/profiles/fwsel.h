/* VENDORED from src/directboot (same rows, same checks) so the probe builds without Directboot.
 * Refresh: make -C Ps4HardwareProbe vendor-check. */
/* fwsel.h - choose the kernel-offset row for the running PS4 system software (see fwsel.c).
 * Pure functions over a pointer to the kernel image, so tools/fwsel_hosttest.c runs the same code on a dump. */
#ifndef DB_FWSEL_H
#define DB_FWSEL_H

#include "types.h"            /* PS4 Payload SDK (libPS4/include) */

#define DB_FW_VERIFIED 1u                  /* row checked against a kernel dump and run on hardware */

/* One PS4 system software version: kernel offsets relative to the kernel base (rows: fwtable.h). */
struct db_kofs {
    u16 ver;                               /* 900 = 9.00 */
    u16 flags;                             /* DB_FW_VERIFIED */
    u32 gadget;                            /* jmp [rsi] (identity check only; 0 = unknown) */
    u32 phys_avail;                        /* 0 = unknown: the stage refuses to arm */
    u32 kmem_alloc, kmem_patch1, kmem_patch2, pstate, kernel_map, kmem_contig, pmap_extract, kernel_pmap;
    u32 icc_nowait, smp_rdv, smp_nobarrier, wlanbt, kern_reboot, pml4pml4i, dmpml4i, dmpdpi;
};

#define DB_ICC_HEAD_MAX  32                /* longest prologue copy the trampoline can hold */
#define DB_HOOK_BYTES    14                /* jmp qword [rip+0] + u64 written over icc_query_nowait */

/* why a row was rejected (first failing check), for the receiver log */
enum { FW_OK = 0, FW_RANGE, FW_GADGET, FW_ICC_HOOKED, FW_PROLOGUE, FW_PATCH1, FW_PATCH2, FW_PSTATE,
       FW_DMAP_GLOBALS, FW_KMAP, FW_ICC_DECODE };

/* Bytes of whole, position-independent instructions at p covering at least `min` bytes; 0 = refuse. */
u32 db_prologue_len(const u8 *p, u32 min);

/* Read-only checks of one row against the kernel at kb (image size kmax). check_data = 0 skips checks of
 * kernel DATA values (kernel_map pointer, DMAP globals): used by the host test, whose dump has no live data. */
u32 db_fw_check(const u8 *kb, u64 kmax, const struct db_kofs *e, int check_data);

/* The single row that passes every check, or 0 (none or ambiguous). *matches = how many rows passed,
 * *why_900 = rejection reason of the 9.00 row (diagnostics). */
const struct db_kofs *db_fw_select(const u8 *kb, u64 kmax, int check_data, u32 *matches, u32 *why_900);

/* 1 if any row's check returns `why` (e.g. FW_ICC_HOOKED, to explain a non-match). */
u32 db_fw_any(const u8 *kb, u64 kmax, u32 why);

/* table rows, for diagnostics */
u32 db_fw_rows(void);
const struct db_kofs *db_fw_row(u32 i);

#endif
