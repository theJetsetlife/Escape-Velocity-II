/* fw.h - firmware detection (detection/), offset profiles + policy (profiles/), runtime validation
 * of the chosen offsets (validation/). Offsets come from src/directboot/fwtable.h (22 versions,
 * 5.05-14.02; only 9.00 hardware-verified) through the unchanged, host-tested fwsel.c. */
#ifndef PRB_FW_H
#define PRB_FW_H

#include "../core/probe.h"

#define FW_K_XFAST  0x1c0          /* IA32_LSTAR - kbase, every version (LEDGER A5, directboot db.h) */
#define FW_KMAX     0x3000000ull   /* fwsel arithmetic bound: text sites < 0x49df80 on every row */

/* detection: version from a SELF image (first bytes of /system/common/lib/libc.sprx), method of
 * ps4-payload-sdk get_firmware(): SCE header fw_version bytes 5/4 as BCD. 0 = not parseable. */
u16 fw_from_self(const u8 *buf, u32 n);

/* profiles: the offset row for a version (exact row, or the row a point release shares by layout;
 * never a nearest match), 0 if none */
u16 fw_row_base(u16 ver);
const void *fw_row_for(u16 ver);
int fw_row_verified(const void *row);
u16 fw_row_ver(const void *row);

/* ring0: identify the running kernel; on success c->kbase/c->fwrow/c->id.fw_state are set */
u32 fw_kernel_identify(struct prb_ctx *c);
/* ring0: derive and prove the direct map from the accepted row; sets c->dmap on success */
u32 fw_dmap_prove(struct prb_ctx *c);

#endif
