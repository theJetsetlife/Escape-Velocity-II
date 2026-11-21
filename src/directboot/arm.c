/* db_arm — ring0, entered once from userland through syscall 11 (jmp [rsi] gadget):
 * db_arm(td, uap) with uap[1] = struct db_arm_args *.
 *
 * Mirrors ps4-linux-loader kernel_main + sys_kexec (REFERENCE_CHAIN 3-7), but in one call:
 *   1. select the kernel-offset row for this system software (fwsel.c: exactly one row whose sites
 *      all have the expected shapes) + DMAP proof + "not already hooked"; refuse versions whose
 *      phys_avail is unknown (every version except the verified 9.00)
 *   2. verify the three patch sites' instruction shapes, then patch (kmem_alloc RWX, pstate 3)
 *   3. kmem_alloc a kernel copy of this blob (code + g_db)
 *   4. kmem_alloc_contig: ELF copy, UPL region, boot block; pmap_extract -> phys
 *   5. fill g_db, copy the blob, install the jmp at icc_query_nowait -> kernel copy of db_hook
 * Any mismatch stops before the first write. Interrupts stay enabled except around the
 * CR0.WP windows (the allocator may sleep: M_WAITOK).
 */
#include "db.h"

struct db_state g_db __attribute__((aligned(64)));
struct db_patch g_patch __attribute__((aligned(64)));
struct db_kofs g_k;                              /* selected row, filled by step 0 */
u8 g_icc_head[DB_ICC_HEAD_MAX];
u32 g_icc_len;

#define DB_KMAX 0x3000000ull   /* arithmetic bound only: fwsel.c reads text sites (< 5 MiB, present on every
                                  version) first and data sites only for a row whose text checks passed */

typedef u64 (*kmem_alloc_t)(u64 map, u64 size);
typedef u64 (*kmem_contig_t)(u64 map, u64 size, int flags, u64 low, u64 high,
                             u64 align, u64 boundary, int memattr);
typedef u64 (*pmap_extract_t)(u64 pmap, u64 va);

#define M_NOWAIT  0x0001
#define M_WAITOK  0x0002
#define M_ZERO    0x0100
#define VM_MEMATTR_WRITE_BACK 6

static int beq(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}

static u64 wp_off(void)
{
    u64 fl;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(fl) :: "memory");
    DB_WRCR(0, DB_RDCR(0) & ~(1ull << 16));
    return fl;
}
static void wp_on(u64 fl)
{
    DB_WRCR(0, DB_RDCR(0) | (1ull << 16));
    __asm__ volatile("pushq %0; popfq" :: "r"(fl) : "memory", "cc");
}

static u64 align_up(u64 v, u64 a) { return (v + a - 1) & ~(a - 1); }

#define FAIL(step, msg) do { rec_s(log, T_ERR, (u64)(step), 0, msg); a->result = -(step); return 0; } while (0)

/* Staged arm (HANDOFF_AUDIT_2 #4): one step per syscall-11 call, each flushed to the PC by
 * user.c before the next starts, so an arm hang names its step in the receiver log
 * (HW runs 19:49, 21:47, 21:53, 22:37, 23:41, 04:27 UTC died somewhere inside the monolithic
 * arm). Every operation is the one the monolithic arm used (same calls, flags, retries); only
 * the cut into steps is new. Steps run from the user mapping, as before.
 *   0 checks (read-only: version row, DMAP proof, patch-site shapes, icc_query_nowait prologue)
 *   1 text patches   2 kmem_alloc kernel copy   3 scanout geometry (DCE reads) + sizes
 *   4-7 kmem_alloc_contig elf / region / boot / fb   8 pmap_extract + ELF copy   9 finish */

int db_arm(void *td, u64 *uap)
{
    (void)td;
    struct db_arm_args *a = (struct db_arm_args *)uap[1];
    if (a->magic != DB_STATE_MAGIC) return 0;
    struct diag_ctx *log = a->log;
    static const char names[4][8] = { "elf", "region", "boot", "fb" };
    u32 st = a->arm_step;
    a->result = -(s64)(100 + st);                   /* stays negative if the step does not finish */

    if (st == 0) {
        static const u8 sig_xfast[8] = { 0x0f,0x01,0xf8,0x65,0x48,0x89,0x24,0x25 };   /* swapgs; mov %rsp,%gs:disp32 */
        u64 kbase = db_rdmsr(0xc0000082) - K_XFAST;
        const u8 *kb = (const u8 *)kbase;
        if ((kbase & 0x3fff) || (kbase >> 32) != 0xffffffffull) FAIL(1, "kbase not plausible");
        if (!beq(kb + K_XFAST, sig_xfast, sizeof sig_xfast)) FAIL(2, "Xfast_syscall entry not at LSTAR (not an OrbisOS kernel?)");
        u32 nmatch, why900;
        const struct db_kofs *k = db_fw_select(kb, DB_KMAX, 1, &nmatch, &why900);
        rec_s(log, T_NOTE, k ? k->ver : 0, (u64)nmatch | (u64)why900 << 8, "version row: ver | matches + 9.00 reject reason<<8");
        if (!k && nmatch == 0) {
            if (db_fw_any(kb, DB_KMAX, FW_ICC_HOOKED)) FAIL(3, "icc_query_nowait already hooked - restart the console before arming again");
            FAIL(2, "system software not recognised (no table row matches this kernel)");
        }
        if (!k) FAIL(2, "more than one table row matches this kernel - refusing");
        g_k = *k;
        g_icc_len = db_prologue_len(kb + g_k.icc_nowait, DB_HOOK_BYTES);
        memcpy(g_icc_head, kb + g_k.icc_nowait, g_icc_len);
        rec_s(log, T_GATE, 1, 1, (g_k.flags & DB_FW_VERIFIED) ? "kernel recognised (verified row)"
                                                               : "kernel recognised (UNVERIFIED row - community offsets)");
        const u8 *p1 = kb + g_k.kmem_patch1 - 2, *p2 = kb + g_k.kmem_patch2 - 1, *ps = kb + g_k.pstate - 1;
        rec_s(log, T_NOTE, (u64)p1[2] | (u64)p2[1] << 8 | (u64)ps[1] << 16, g_icc_len,
              "patch sites before (p1|p2<<8|pstate<<16) | icc bytes displaced");
        /* refuse BEFORE the DMAP proof: that dereferences an address built from the row's globals, and for an
         * unverified row a wrong-but-plausible value could fault in ring 0 */
        if (!g_k.phys_avail)
            FAIL(13, "recognised, but phys_avail is unknown for this version: not arming (see docs/FIRMWARE_VERSIONS.md)");
        u32 dmpml4i = *(const u32 *)(kb + g_k.dmpml4i), dmpdpi = *(const u32 *)(kb + g_k.dmpdpi);
        u64 dmap = 0xffff800000000000ull | (u64)dmpml4i << 39 | (u64)dmpdpi << 30;
        u64 cr3 = DB_RDCR(3) & 0x000ffffffffff000ull;
        u64 self = ((const u64 *)(dmap + cr3))[*(const u32 *)(kb + g_k.pml4pml4i) & 511];
        if (!(self & 1) || (self & 0x000ffffffffff000ull) != cr3) FAIL(4, "DMAP proof failed");
        rec_s(log, T_NOTE, kbase, dmap, "kbase|dmap");
        u64 kernel_map = *(const u64 *)(kb + g_k.kernel_map);
        a->s_kbase = kbase; a->s_dmap = dmap; a->s_kmap = kernel_map;
        a->result = 0;
        return 0;
    }

    u64 kbase = a->s_kbase, dmap = a->s_dmap, kernel_map = a->s_kmap;
    const u8 *kb = (const u8 *)kbase;
    if (!kbase || db_rdmsr(0xc0000082) - K_XFAST != kbase) FAIL(11, "step without a valid step 0");

    switch (st) {
    case 1: {
        u64 fl = wp_off();
        *(volatile u8 *)(kbase + g_k.kmem_patch1) = 7;   /* kmem_alloc maps RWX (reference) */
        *(volatile u8 *)(kbase + g_k.kmem_patch2) = 7;
        *(volatile u8 *)(kbase + g_k.pstate) = 3;        /* pstate before shutdown (reference) */
        wp_on(fl);
        rec_s(log, T_GATE, 2, 1, "kmem_alloc RWX + pstate patches applied");
        break;
    }
    case 2: {
        u64 blob_size = align_up((u64)(__blob_end - _start), 0x1000);
        u64 kblob = ((kmem_alloc_t)(kbase + g_k.kmem_alloc))(kernel_map, blob_size);
        if (!kblob || (kblob >> 47) != 0x1ffff) FAIL(7, "kmem_alloc for the blob failed");
        a->s_kblob = kblob; a->s_blob_size = blob_size;
        rec_s(log, T_NOTE, kblob, blob_size, "kernel blob va|size");
        break;
    }
    case 3:
        g_db.dmap = dmap;
        g_db.fb_base = 0; g_db.fb_w = g_db.fb_h = g_db.fb_pitch = g_db.fb_crtc = 0;
        db_find_scanout(&g_db);                        /* geometry of the active pipe */
        a->s_want[0] = align_up(a->elf_size, 0x1000);
        a->s_want[1] = 0x100000;   /* entry image (~36 KiB) + 128 KiB HOBs + stack; FVs stay in the ELF copy (run 05:56) */
        a->s_want[2] = 0x10000;                        /* PML4 + PDPT + GDT */
        a->s_want[3] = align_up((u64)(g_db.fb_pitch ? g_db.fb_pitch : 7680) * (g_db.fb_h ? g_db.fb_h : 1080), 0x10000);
        rec_s(log, T_NOTE, g_db.fb_base, (u64)g_db.fb_w << 32 | g_db.fb_h, "scanout at arm: base|w<<32|h");
        break;
    case 4: case 5: case 6: case 7: {
        /* M_NOWAIT only. HW run 05:50 UTC (staged arm): the console DIED (no signal) inside
         * arm5 = 8.1 MiB contig with M_WAITOK; the heap runs 02:05-02:18 died the same way on
         * 16 MiB M_WAITOK. When the request can't be met at once, OrbisOS's waiting/reclaim path
         * kills the console. M_NOWAIT failures are survivable (run 01:50: 8 x 16 MiB failed, the
         * console armed normally). On failure the step returns DB_ARM_RETRY and user.c sleeps
         * and calls the same step again; it gives up cleanly ("NOT armed") after ~10 s. */
        u32 k = st - 4, tries = 0;
        kmem_contig_t contig = (kmem_contig_t)(kbase + g_k.kmem_contig);
        u64 v = 0;
        while (!v && tries < 4) {
            /* display buffers must be 64 KiB aligned (SDK VideoOut docs; GRAPHICS_SDK_NOTES) */
            v = contig(kernel_map, a->s_want[k], M_ZERO | M_NOWAIT, 0, ~0ull, k == 3 ? 0x10000 : 0x1000, 0,
                       VM_MEMATTR_WRITE_BACK);
            tries++;
        }
        rec_s(log, T_NOTE, v, a->s_want[k] | (u64)tries << 48, names[k]);   /* b = size | tries<<48 */
        if (!v && k == 3) {
            /* HW run 06:04 UTC: no 7.9 MiB contiguous block either (40/40). Reuse the surface
             * OrbisOS is scanning out: GPU-shared UC memory (run 3: 0x58e00000-0x7fffffff, kept
             * reserved by the memory map), double-buffered 0x7f8000 apart, so one buffer holds
             * a linear 1920x1080x4 (0x7e9000). display_takeover switches it to linear. */
            u64 lin = align_up((u64)g_db.fb_pitch * g_db.fb_h, 0x1000);
            u64 sb = g_db.fb_base;
            if (sb >= 0x58e00000ull && sb + lin <= 0x80000000ull && !(sb & 0xfff) && lin && lin <= 0x7f8000ull) {
                a->s_va[3] = 0; a->s_pa[3] = sb; a->s_want[3] = lin;
                rec_s(log, T_NOTE, sb, lin, "fb: reusing the OrbisOS scanout surface pa|size (no contig block)");
                break;
            }
            rec_s(log, T_NOTE, sb, lin, "fb: scanout surface not usable as fallback base|size");
        }
        if (!v) { a->result = DB_ARM_RETRY; return 0; }
        a->s_va[k] = v;
        break;
    }
    case 8: {
        pmap_extract_t extract = (pmap_extract_t)(kbase + g_k.pmap_extract);
        u64 pmap = kbase + g_k.kernel_pmap;
        for (u32 k = 0; k < 4; k++) {
            if (k == 3 && !a->s_va[3] && a->s_pa[3]) continue;   /* fb = OrbisOS scanout (pa set in arm7) */
            a->s_pa[k] = extract(pmap, a->s_va[k]);
            if (!a->s_pa[k]) FAIL(9, "pmap_extract returned 0");
        }
        memcpy((void *)a->s_va[0], a->elf, a->elf_size);
        rec_s(log, T_NOTE, a->s_pa[0], a->s_want[0], "elf pa|size");
        rec_s(log, T_NOTE, a->s_pa[1], a->s_want[1], "region pa|size");
        rec_s(log, T_NOTE, a->s_pa[2], a->s_want[2], "boot block pa|size");
        rec_s(log, T_NOTE, a->s_pa[3], a->s_want[3], "own framebuffer pa|size");
        break;
    }
    case 9: {
        if (!g_icc_len || !beq(kb + g_k.icc_nowait, g_icc_head, g_icc_len)) FAIL(10, "icc_query_nowait changed since step 0");
        u64 keep_fb = g_db.fb_base; u32 kw = g_db.fb_w, kh = g_db.fb_h, kp = g_db.fb_pitch, kc = g_db.fb_crtc;
        memset(&g_db, 0, sizeof g_db);
        g_db.fb_base = keep_fb; g_db.fb_w = kw; g_db.fb_h = kh; g_db.fb_pitch = kp; g_db.fb_crtc = kc;
        g_db.fbo_pa = a->s_pa[3]; g_db.fbo_size = a->s_want[3];
        g_db.heap_n = 0;                               /* UEFI heap disabled (runs 02:05-02:18) */
        g_db.magic = DB_STATE_MAGIC;
        g_db.kbase = kbase; g_db.dmap = dmap;
        g_db.kblob = a->s_kblob; g_db.kblob_size = a->s_blob_size;
        g_db.elf_pa = a->s_pa[0]; g_db.elf_size = a->elf_size;
        g_db.region_pa = a->s_pa[1]; g_db.region_size = a->s_want[1];
        g_db.boot_pa = a->s_pa[2]; g_db.boot_size = a->s_want[2];
        g_db.unix_time = a->unix_time; g_db.tsc_hz = a->tsc_hz;
        /* trampoline: the displaced whole instructions (position independent, fwsel.c) + jmp back */
        memcpy(db_tramp, g_icc_head, g_icc_len);
        db_tramp[g_icc_len] = 0xff; db_tramp[g_icc_len + 1] = 0x25;   /* jmp qword ptr [rip+0] */
        *(u32 *)(db_tramp + g_icc_len + 2) = 0;
        *(u64 *)(db_tramp + g_icc_len + 6) = kbase + g_k.icc_nowait + g_icc_len;
        db_takeover = 0;
        memset(&g_patch, 0, sizeof g_patch);
        g_patch.site = kbase + g_k.icc_nowait;
        g_patch.target = a->s_kblob + ((u64)db_hook_entry - (u64)_start);
        memcpy((void *)a->s_kblob, _start, (u64)(__blob_end - _start));
        rec_s(log, T_NOTE, g_patch.site, g_patch.target, "hook site|target (installed next, CPUs stopped)");
        rec_s(log, T_GATE, 3, 1, "staged: kernel copy + allocations ready");
        break;
    }
    default:
        FAIL(12, "unknown arm step");
    }
    a->result = 0;
    return 0;
}

/* Reset the wifi/bt chip through OrbisOS's own driver, as the reference hook does first
 * (linux_boot.c:454 "we need reset bt/wifi"): stops the SDIO wifi DMA before the takeover, so it
 * cannot write into EDK II's memory once the IOMMU is off. Must run while OrbisOS is alive and
 * NOT inside the hook (it talks ICC; icc_query_nowait is hooked). It drops the wifi link to the
 * receiver, so user.c calls it after the last log flush. Offset from the selected row (9.00:
 * ps4-linux-loader magic.h kern_off_wlanbt 0x180860, prologue checked against kernel_900_full.bin). */
int db_wlanbt(void *td, u64 *uap)
{
    (void)td;
    struct db_arm_args *a = (struct db_arm_args *)uap[1];
    if (a->magic != DB_STATE_MAGIC || a->result != 0 || !g_k.wlanbt) return 0;
    u64 kbase = db_rdmsr(0xc0000082) - K_XFAST;
    static const u8 sig[4] = { 0x55, 0x48,0x89,0xe5 };             /* push rbp; mov rbp,rsp (also checked at arm) */
    if (!beq((const u8 *)(kbase + g_k.wlanbt), sig, sizeof sig)) return 0;
    ((void (*)(unsigned int))(kbase + g_k.wlanbt))(2);
    return 0;
}

/* Second syscall-11 entry: take over NOW, while OrbisOS still drives the display.
 * HW run 19:23 UTC: going through OrbisOS's shutdown (kern_reboot -> shutdown_final ->
 * icc_query_nowait) armed and rebooted fine, but OrbisOS switches HDMI off before
 * shutdown_final, so the takeover ran on a dark screen. Userland sync()s the disks first;
 * then we enter the kernel copy of db_hook directly (same rendezvous + quiesce + handoff). */
int db_reboot(void *td, u64 *uap)
{
    (void)td;
    struct db_arm_args *a = (struct db_arm_args *)uap[1];
    if (a->magic != DB_STATE_MAGIC || a->result != 0) return 0;
    u64 kbase = db_rdmsr(0xc0000082) - K_XFAST;
    if (!g_k.icc_nowait) return 0;
    const u8 *site = (const u8 *)(kbase + g_k.icc_nowait);
    if (!(site[0] == 0xff && site[1] == 0x25)) return 0;                       /* not armed */
    volatile u32 *kflag = (volatile u32 *)(g_db.kblob + ((u64)&db_takeover - (u64)_start));
    *kflag = 1;                                     /* the hook path would take over too */
    __asm__ volatile("mfence" ::: "memory");
    int (*khook)(u8 *) = (int (*)(u8 *))(g_db.kblob + ((u64)db_hook - (u64)_start));
    khook((u8 *)0);                                 /* does not return */
    return 0;
}

/* ---- hook install with every CPU stopped (HW runs 19:49 / 21:47 / 21:53 UTC: intermittent
 * hang inside arm; icc_query_nowait has runtime callers, so patching its first 14 bytes while
 * other CPUs run can let one execute a half-written instruction -> panic). ---- */
void db_patch_action(void *arg)
{
    struct db_patch *p = arg;
    u64 fl;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(fl) :: "memory");
    __atomic_fetch_add(&p->arrived, 1, __ATOMIC_SEQ_CST);
    for (u64 spin = 0; p->arrived < DB_NCPU && spin < 200000000ull; spin++) __asm__ volatile("pause");
    if (__atomic_exchange_n(&p->writer, 1, __ATOMIC_SEQ_CST) == 0) {
        DB_WRCR(0, DB_RDCR(0) & ~(1ull << 16));
        *(volatile u16 *)p->site = 0x25ff;                /* jmp qword ptr [rip+0] */
        *(volatile u32 *)(p->site + 2) = 0;
        *(volatile u64 *)(p->site + 6) = p->target;
        __asm__ volatile("wbinvd" ::: "memory");
        DB_WRCR(0, DB_RDCR(0) | (1ull << 16));
        __atomic_store_n(&p->done, 1, __ATOMIC_SEQ_CST);
    } else {
        for (u64 spin = 0; !p->done && spin < 400000000ull; spin++) __asm__ volatile("pause");
    }
    __asm__ volatile("pushq %0; popfq" :: "r"(fl) : "memory", "cc");
}

int db_install(void *td, u64 *uap)
{
    (void)td;
    struct db_arm_args *a = (struct db_arm_args *)uap[1];
    if (a->magic != DB_STATE_MAGIC || a->result != 0) return 0;
    struct diag_ctx *log = a->log;
    u64 kbase = db_rdmsr(0xc0000082) - K_XFAST;
    const u8 *kb = (const u8 *)kbase;
    if (!g_icc_len || !beq(kb + g_k.icc_nowait, g_icc_head, g_icc_len)) { a->result = -11; rec_s(log, T_ERR, 11, 0, "icc_query_nowait changed since arm"); return 0; }
    /* everything the other CPUs touch must be kernel memory: action + control block in kblob */
    void (*kact)(void *) = (void (*)(void *))(g_db.kblob + ((u64)db_patch_action - (u64)_start));
    struct db_patch *kp = (struct db_patch *)(g_db.kblob + ((u64)&g_patch - (u64)_start));
    void (*nb)(void *) = (void (*)(void *))(kbase + g_k.smp_nobarrier);
    ((void (*)(void (*)(void *), void (*)(void *), void (*)(void *), void *))(kbase + g_k.smp_rdv))(nb, kact, nb, kp);
    const u8 *site = (const u8 *)(kbase + g_k.icc_nowait);
    int ok = site[0] == 0xff && site[1] == 0x25 && *(const u64 *)(site + 6) == kp->target;
    rec_s(log, T_NOTE, kp->arrived, kp->done, "rendezvous: cpus arrived | patch done");
    if (!ok) { a->result = -12; rec_s(log, T_ERR, 12, 0, "hook bytes not in place after rendezvous"); return 0; }
    rec_s(log, T_GATE, 3, 1, "ARMED: icc_query_nowait -> db_hook (installed with all CPUs stopped)");
    return 0;
}
