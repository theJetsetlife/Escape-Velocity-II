/* fwsel.c - pick the kernel-offset row (fwtable.h) for the running system software, read-only.
 *
 * A row is accepted only if every site it names has the instruction shape the stage depends on:
 *   - functions it calls start with a frame prologue (push rbp; mov rbp,rsp),
 *   - kmem_alloc's two protection operands are `mov $3|7,%r9d` / `push $3|7; call`,
 *   - the pstate argument is `mov $1|3,%edi; call`,
 *   - icc_query_nowait's first >= 14 bytes decode into whole position-independent instructions
 *     (they are copied into the trampoline), and the function is not hooked already,
 *   - kernel_map holds a kernel pointer and the DMAP globals are plausible (live kernel only).
 * Exactly one row must pass. The DMAP proof (arm.c step 0) then confirms the page-table globals.
 * Nothing here writes; a version that matches no row, or more than one, is refused.
 */
#include "fwtable.h"
#include "fwsel.h"

static int prologue(const u8 *p) { return p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89 && p[3] == 0xe5; }

/* ModRM (+SIB +displacement) length; 0 = RIP-relative (not position independent) */
static u32 modrm_len(const u8 *m)
{
    u32 mod = m[0] >> 6, rm = m[0] & 7, n = 1;
    if (mod == 3) return 1;
    if (rm == 4) {                                   /* SIB */
        n++;
        if (mod == 0 && (m[1] & 7) == 5) return n + 4;
    } else if (mod == 0 && rm == 5) {
        return 0;                                    /* [rip + disp32] */
    }
    return n + (mod == 1 ? 1 : mod == 2 ? 4 : 0);
}

u32 db_prologue_len(const u8 *p, u32 min)
{
    u32 len = 0;
    while (len < min) {
        const u8 *q = p + len;
        u32 n = 0, rexw = 0;
        if ((q[0] & 0xf0) == 0x40) { rexw = q[0] & 8; n = 1; }
        u8 op = q[n];
        u32 ml;
        if (op >= 0x50 && op <= 0x57) {
            n += 1;                                              /* push r */
        } else if (op >= 0xb8 && op <= 0xbf) {
            n += 1 + (rexw ? 8 : 4);                             /* mov r, imm */
        } else if (op == 0x01 || op == 0x03 || op == 0x09 || op == 0x0b || op == 0x21 || op == 0x23 ||
                   op == 0x29 || op == 0x2b || op == 0x31 || op == 0x33 || op == 0x39 || op == 0x3b ||
                   op == 0x85 || op == 0x88 || op == 0x89 || op == 0x8a || op == 0x8b || op == 0x8d) {
            if (!(ml = modrm_len(q + n + 1))) return 0;          /* ALU / mov / test / lea r/m */
            n += 1 + ml;
        } else if (op == 0x80 || op == 0x83 || op == 0xc6) {
            if (!(ml = modrm_len(q + n + 1))) return 0;          /* group 1 / mov r/m, imm8 */
            n += 1 + ml + 1;
        } else if (op == 0x81 || op == 0xc7) {
            if (!(ml = modrm_len(q + n + 1))) return 0;          /* group 1 / mov r/m, imm32 */
            n += 1 + ml + 4;
        } else if (op == 0x0f && (q[n + 1] == 0xb6 || q[n + 1] == 0xb7 || q[n + 1] == 0xbe || q[n + 1] == 0xbf)) {
            if (!(ml = modrm_len(q + n + 2))) return 0;          /* movzx / movsx */
            n += 2 + ml;
        } else {
            return 0;                                            /* branch, call, prefix, unknown */
        }
        len += n;
        if (len > DB_ICC_HEAD_MAX) return 0;
    }
    return len;
}

u32 db_fw_check(const u8 *kb, u64 kmax, const struct db_kofs *e, int check_data)
{
    const u32 offs[] = { e->kmem_alloc, e->kmem_patch1, e->kmem_patch2, e->pstate, e->kernel_map, e->kmem_contig,
                         e->pmap_extract, e->kernel_pmap, e->icc_nowait, e->smp_rdv, e->smp_nobarrier, e->wlanbt,
                         e->kern_reboot, e->pml4pml4i, e->dmpml4i, e->dmpdpi, e->phys_avail, e->gadget };
    for (u32 i = 0; i < sizeof offs / sizeof offs[0]; i++)
        if (offs[i] + 64ull > kmax) return FW_RANGE;
    if (e->gadget && !(kb[e->gadget] == 0xff && kb[e->gadget + 1] == 0x26)) return FW_GADGET;
    const u8 *icc = kb + e->icc_nowait;
    if (icc[0] == 0xe9 || (icc[0] == 0xff && icc[1] == 0x25)) return FW_ICC_HOOKED;
    if (!prologue(icc) || !prologue(kb + e->kmem_alloc) || !prologue(kb + e->kmem_contig) ||
        !prologue(kb + e->pmap_extract) || !prologue(kb + e->smp_rdv) || !prologue(kb + e->smp_nobarrier) ||
        !prologue(kb + e->wlanbt) || !prologue(kb + e->kern_reboot))
        return FW_PROLOGUE;
    const u8 *p1 = kb + e->kmem_patch1 - 2, *p2 = kb + e->kmem_patch2 - 1, *ps = kb + e->pstate - 1;
    if (!(p1[0] == 0x41 && p1[1] == 0xb9 && (p1[2] == 3 || p1[2] == 7) && !p1[3] && !p1[4] && !p1[5])) return FW_PATCH1;
    if (!(p2[0] == 0x6a && (p2[1] == 3 || p2[1] == 7) && p2[2] == 0xe8)) return FW_PATCH2;
    if (!(ps[0] == 0xbf && (ps[1] == 1 || ps[1] == 3) && !ps[2] && !ps[3] && !ps[4] && ps[5] == 0xe8)) return FW_PSTATE;
    if (!db_prologue_len(icc, DB_HOOK_BYTES)) return FW_ICC_DECODE;
    if (check_data) {
        u32 pml4 = *(const u32 *)(kb + e->pml4pml4i), dml4 = *(const u32 *)(kb + e->dmpml4i);
        u32 ddpi = *(const u32 *)(kb + e->dmpdpi);
        if (pml4 < 256 || pml4 > 511 || dml4 < 256 || dml4 > 511 || ddpi > 511 || pml4 == dml4) return FW_DMAP_GLOBALS;
        if ((*(const u64 *)(kb + e->kernel_map) >> 47) != 0x1ffff) return FW_KMAP;
    }
    return FW_OK;
}

const struct db_kofs *db_fw_select(const u8 *kb, u64 kmax, int check_data, u32 *matches, u32 *why_900)
{
    const struct db_kofs *hit = 0;
    u32 n = 0;
    *why_900 = FW_RANGE;
    for (u32 i = 0; i < DB_FW_COUNT; i++) {
        u32 r = db_fw_check(kb, kmax, &db_fw_table[i], check_data);
        if (db_fw_table[i].ver == 900) *why_900 = r;
        if (r == FW_OK) { hit = &db_fw_table[i]; n++; }
    }
    *matches = n;
    return n == 1 ? hit : 0;
}

u32 db_fw_any(const u8 *kb, u64 kmax, u32 why)
{
    for (u32 i = 0; i < DB_FW_COUNT; i++)
        if (db_fw_check(kb, kmax, &db_fw_table[i], 0) == why) return 1;
    return 0;
}

u32 db_fw_rows(void) { return (u32)DB_FW_COUNT; }
const struct db_kofs *db_fw_row(u32 i) { return i < DB_FW_COUNT ? &db_fw_table[i] : 0; }
