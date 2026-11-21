/* db_hook — runs from the KERNEL COPY of this blob when OrbisOS's shutdown_final calls the
 * hooked icc_query_nowait (LEDGER D1-D3). Reference: ps4-linux-loader hook_icc_query_nowait +
 * cpu_quiesce_gate (REFERENCE_CHAIN 9-17), with three deliberate differences:
 *   - configure_vram (2 GiB at 4 GiB, as the reference) so the high DRAM above it can be
 *     released as RAM; EDK II keeps driving the existing below-4G scanout,
 *   - the framebuffer window is made UC by a variable MTRR (it lives in WB DRAM, run 3; UC is the
 *     only type defined when it overlaps the 0-2 GiB WB range),
 *   - the jump target is upl_load (EDK II UPL + HOB list), not the Linux thunk.
 * Progress is shown as coloured squares on the current scanout (top-left, one per step).
 */
#include "db.h"
#include "memmap.h"
#include "hda.h"

#define PA_MASK 0x000ffffffffff000ull

typedef void (*smp_fn_t)(void *);
typedef void (*smp_rdv_t)(smp_fn_t setup, smp_fn_t action, smp_fn_t teardown, void *arg);

static inline void *dm(struct db_state *s, u64 pa) { return (void *)(s->dmap + pa); }
static inline void w32(struct db_state *s, u64 pa, u32 v) { *(volatile u32 *)dm(s, pa) = v; }
static inline void w64(struct db_state *s, u64 pa, u64 v) { *(volatile u64 *)dm(s, pa) = v; }
static inline u32  r32(struct db_state *s, u64 pa) { return *(volatile u32 *)dm(s, pa); }
static inline void m32(struct db_state *s, u64 pa, u32 clr, u32 set) { w32(s, pa, (r32(s, pa) & ~clr) | set); }

static void delay_ms(struct db_state *s, u32 ms)
{
    u64 hz = s->tsc_hz ? s->tsc_hz : 1593719000ull, t0, t;
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); t0 = ((u64)hi << 32) | lo;
    do { __asm__ volatile("pause; rdtsc" : "=a"(lo), "=d"(hi)); t = ((u64)hi << 32) | lo; }
    while (t - t0 < hz / 1000 * ms);
}

static void delay_us_cb(void *ctx, u32 us)              /* hda.c callback, TSC based */
{
    struct db_state *s = ctx;
    u64 hz = s->tsc_hz ? s->tsc_hz : 1593719000ull, t0, t;
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); t0 = ((u64)hi << 32) | lo;
    do { __asm__ volatile("pause; rdtsc" : "=a"(lo), "=d"(hi)); t = ((u64)hi << 32) | lo; }
    while (t - t0 < hz / 1000000 * us);
}

/* ---- on-screen progress ---- */
static void marker(struct db_state *s, u32 step, u32 rgb)
{
    if (!s->fb_base || s->fb_pitch < 64 * 4) return;
    u8 *fb = dm(s, s->fb_base);
    u32 x0 = 48 + step * 72, y0 = 48;
    for (u32 y = y0; y < y0 + 56 && y < s->fb_h; y++) {
        u32 *row = (u32 *)(fb + (u64)y * s->fb_pitch);
        for (u32 x = x0; x < x0 + 56 && x < s->fb_w; x++) row[x] = 0xff000000u | rgb;
    }
    __asm__ volatile("sfence" ::: "memory");
}
#define C_GREEN 0x00c000
#define C_BLUE  0x2060ff
#define C_WHITE 0xffffff
#define C_RED   0xff0000
#define C_YELL  0xffd000
#define C_GREY  0x606060

static const u16 crtc_off[6] = { 0x0, 0x300, 0x2600, 0x2900, 0x2c00, 0x2f00 };   /* cikd.h */

/* ---- scanout: the enabled CRTC with the largest surface (run 2/3: CRTC1 1080p) ---- */
void db_find_scanout(struct db_state *s)
{
    const u16 *off = crtc_off;
    for (u32 n = 0; n < 6; n++) {
        u64 r = HW_GPU_MMIO;
        u32 en = r32(s, r + (0x1a00u + off[n]) * 4);
        u32 lo = r32(s, r + (0x1a04u + off[n]) * 4), hi = r32(s, r + (0x1a07u + off[n]) * 4);
        u32 pitch = r32(s, r + (0x1a06u + off[n]) * 4);
        u32 xe = r32(s, r + (0x1a0du + off[n]) * 4), ye = r32(s, r + (0x1a0eu + off[n]) * 4);
        if (!(en & 1) || xe < 640 || ye < 480 || xe > 4096 || ye > 4096) continue;
        if ((u64)xe * ye <= (u64)s->fb_w * s->fb_h) continue;
        /* GRPH_PRIMARY_SURFACE_ADDRESS is the programmed (pending) surface; the one actually
         * scanned out is GRPH_SURFACE_ADDRESS_INUSE(_HIGH) (dce_8_0_d.h 0x1a13 / 0x1a18) */
        {
            u32 ilo = r32(s, r + (0x1a13u + off[n]) * 4), ihi = r32(s, r + (0x1a18u + off[n]) * 4);
            if (ilo & ~0xffu) { lo = ilo; hi = ihi; }
        }
        s->fb_base = ((u64)hi << 32) | (lo & ~0xffu);
        s->fb_w = xe; s->fb_h = ye; s->fb_pitch = pitch * 4; s->fb_crtc = n;
    }
}

/* ---- scanout -> linear (HW run 19:3x UTC: OrbisOS scans out a 2D-tiled surface, GRPH_CONTROL
 * ARRAY_MODE 4, so linear writes showed as tearing). Same register sequence as amdgpu
 * dce_v8_0_crtc_do_set_base for ARGB8888 / LINEAR_ALIGNED, applied under GRPH_UPDATE_LOCK. ---- */
#define GRPH(reg) (HW_GPU_MMIO + ((u64)(reg) + crtc_off[s->fb_crtc]) * 4)

/* Move the active pipe onto OUR linear framebuffer (allocated + reserved at arm). Same register
 * sequence as amdgpu dce_v8_0_crtc_do_set_base (ARGB8888, LINEAR_ALIGNED), plus: overlay plane
 * off and both update locks released, because OrbisOS may compose on OVL and/or hold
 * MASTER_UPDATE_LOCK (HW run 19:41 UTC: our writes to the tiled surface never latched). */
static void display_takeover(struct db_state *s)
{
    if (!s->fbo_pa || s->fb_crtc > 5 || !s->fb_w || !s->fb_h) return;
    /* code review: the hook re-reads the pipe geometry; never let it exceed what arm reserved
     * (fill, markers, console and the EDK II graphics HOB all derive from fb_h / fb_pitch) */
    if (!s->fb_pitch || (u64)s->fb_w * 4 > s->fb_pitch) return;
    if ((u64)s->fb_pitch * s->fb_h > s->fbo_size) s->fb_h = (u32)(s->fbo_size / s->fb_pitch);
    u64 old = s->fb_base;
    u8 *fb = dm(s, s->fbo_pa);
    for (u32 y = 0; y < s->fb_h; y++) {                  /* black */
        u32 *row = (u32 *)(fb + (u64)y * s->fb_pitch);
        for (u32 x = 0; x < s->fb_w; x++) row[x] = 0xff000000u;
    }
    u32 ctl = r32(s, GRPH(0x1a01));
    u32 lin = 2u | (0u << 8) | (1u << 20) | (ctl & (0x1fu << 24));
    m32(s, GRPH(0x1a11), 0, 0x10000);                    /* GRPH_UPDATE_LOCK */
    w32(s, GRPH(0x1a12), 0);                             /* FLIP_CONTROL */
    w32(s, GRPH(0x1a07), (u32)(s->fbo_pa >> 32));
    w32(s, GRPH(0x1a08), (u32)(s->fbo_pa >> 32));
    w32(s, GRPH(0x1a04), (u32)s->fbo_pa & ~0xffu);
    w32(s, GRPH(0x1a05), (u32)s->fbo_pa & ~0xffu);
    w32(s, GRPH(0x1a01), lin);                           /* GRPH_CONTROL: linear ARGB8888 */
    w32(s, GRPH(0x1a03), 0);                             /* GRPH_SWAP_CNTL */
    w32(s, GRPH(0x1a09), 0); w32(s, GRPH(0x1a0a), 0);
    w32(s, GRPH(0x1a0b), 0); w32(s, GRPH(0x1a0c), 0);
    w32(s, GRPH(0x1a0d), s->fb_w); w32(s, GRPH(0x1a0e), s->fb_h);
    w32(s, GRPH(0x1a06), s->fb_pitch / 4);
    m32(s, GRPH(0x1a00), 0, 1);                          /* GRPH_ENABLE */
    w32(s, GRPH(0x1a1c), 0);            /* OVL_ENABLE: overlay plane off */
    m32(s, GRPH(0x1a11), 0x10000, 0);                    /* release GRPH_UPDATE_LOCK */
    {
        w32(s, GRPH(0x1bbe), 0);                         /* MASTER_UPDATE_MODE: any time in vblank */
        w32(s, GRPH(0x1bbd), 0);                         /* MASTER_UPDATE_LOCK released */
    }
    for (u32 i = 0; i < 200; i++) {       /* wait for the pipe to scan us out */
        u64 inuse = ((u64)r32(s, GRPH(0x1a18)) << 32) | (r32(s, GRPH(0x1a13)) & ~0xffu);
        if (inuse == (s->fbo_pa & ~0xffull)) { s->fbo_latched = 1; break; }
        delay_ms(s, 1);
    }
    if (!s->fbo_latched && old) {                        /* still on the old surface: show that */
        u64 keep = s->fb_base; s->fb_base = old;
        marker(s, 6, 0xffd000); marker(s, 7, 0xffd000);
        s->fb_base = keep;
    }
    s->fb_base = s->fbo_pa;                               /* everything from here (and EDK II) */
}

/* ---- MTRR: make the scanout window uncacheable (same value on every CPU) ---- */
static void fb_mtrr(struct db_state *s)
{
    if (!s->fbo_pa) return;
    u64 cap = db_rdmsr(0xfe);
    u32 n = (u32)cap & 0xff, slot = 0xff;
    for (u32 i = 0; i < n && i < 16; i++)
        if (!(db_rdmsr(0x201 + 2 * i) & 0x800)) { slot = i; break; }
    if (slot == 0xff) return;
    u64 size = 1ull << 20;                                 /* smallest aligned window holding our fb */
    while ((s->fbo_pa & ~(size - 1)) + size < s->fbo_pa + s->fbo_size) size <<= 1;
    u64 base = s->fbo_pa & ~(size - 1);
    u64 cr0 = DB_RDCR(0);
    DB_WRCR(0, (cr0 | (1ull << 30)) & ~(1ull << 29));    /* CD=1 NW=0 */
    __asm__ volatile("wbinvd" ::: "memory");
    u64 def = db_rdmsr(0x2ff);
    db_wrmsr(0x2ff, def & ~0x800ull);                      /* MTRRs off */
    db_wrmsr(0x200 + 2 * slot, base | 0);                  /* type 0 = UC (overlap with WB -> UC) */
    db_wrmsr(0x201 + 2 * slot, (~(size - 1) & 0x000000fffffff000ull) | 0x800);
    __asm__ volatile("wbinvd" ::: "memory");
    db_wrmsr(0x2ff, def);
    DB_WRCR(0, cr0);
}

/* ---- VRAM carve UC on every CPU (reference setup_mtrr MTRR 3), same slot search as fb_mtrr ---- */
static void vram_mtrr(struct db_state *s)
{
    (void)s;
    u64 cap = db_rdmsr(0xfe);
    u32 n = (u32)cap & 0xff, slot = 0xff;
    for (u32 i = 0; i < n && i < 16; i++)
        if (!(db_rdmsr(0x201 + 2 * i) & 0x800)) { slot = i; break; }
    if (slot == 0xff) return;
    u64 cr0 = DB_RDCR(0);
    DB_WRCR(0, (cr0 | (1ull << 30)) & ~(1ull << 29));
    __asm__ volatile("wbinvd" ::: "memory");
    u64 def = db_rdmsr(0x2ff);
    db_wrmsr(0x2ff, def & ~0x800ull);
    db_wrmsr(0x200 + 2 * slot, DB_VRAM_BASE | 0);         /* UC */
    db_wrmsr(0x201 + 2 * slot, (~(DB_VRAM_SIZE - 1) & 0x000000fffffff000ull) | 0x800);
    __asm__ volatile("wbinvd" ::: "memory");
    db_wrmsr(0x2ff, def);
    DB_WRCR(0, cr0);
}

/* ---- GPU memory controller: VRAM aperture -> the carve (reference configure_vram, values for
 * 2048 MiB). The live scanout (MC address 0x6e800000) is outside the new FB range and keeps going
 * through the system aperture. Runs after the GPU engines are halted. ---- */
static void configure_vram(struct db_state *s)
{
    const u64 fb_base = 0x0f00000000ull, fb_top = fb_base + DB_VRAM_SIZE - 1;
    w32(s, HW_GPU_MMIO + 0x2024, 0);                                       /* MC_VM_FB_LOCATION */
    w32(s, HW_GPU_MMIO + 0x2c04, 0);                                       /* HDP_NONSURFACE_BASE */
    w32(s, HW_GPU_MMIO + 0x2024, (u32)(((fb_top >> 24) << 16) | (fb_base >> 24)));
    w32(s, HW_GPU_MMIO + 0x2068, (u32)(DB_VRAM_BASE >> 22));               /* MC_VM_FB_OFFSET */
    w32(s, HW_GPU_MMIO + 0x2c04, (u32)(fb_base >> 8));
    w32(s, HW_GPU_MMIO + 0x5428, (u32)(DB_VRAM_SIZE >> 20));               /* CONFIG_MEMSIZE (MiB) */
}

/* ---- per-CPU interrupt cleanup (reference cleanup_interrupts) ---- */
static void lapic_quiet(struct db_state *s)
{
    w32(s, HW_LAPIC + 0x410, 1);
    for (u32 i = 0x320; i < 0x380; i += 0x10) w32(s, HW_LAPIC + i, 0x10000);   /* mask LVTs */
    for (u32 i = 0x480; i < 0x500; i += 0x10) w32(s, HW_LAPIC + i, 0xffffffff);
    for (u32 i = 0x500; i < 0x540; i += 0x10) w32(s, HW_LAPIC + i, 0x10000);
    w32(s, HW_LAPIC + 0x410, 0);
    db_wrmsr(0x413, (1ull << 24) | (1ull << 52));
    db_wrmsr(0xc0000408, (1ull << 24) | (1ull << 52));
}

/* ---- memory map: inputs gathered here (live DMAP, phys_avail, reserved ranges), the decisions
 * live in memmap.c so tools/memmap_hosttest.c can check them off the console ---- */

/* exact VA -> PA under the current CR3 */
static u64 va2pa(struct db_state *s, u64 va)
{
    u64 *t = dm(s, DB_RDCR(3) & PA_MASK), e = t[(va >> 39) & 511];
    if (!(e & 1)) return 0;
    t = dm(s, e & PA_MASK); e = t[(va >> 30) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) return (e & PA_MASK & ~((1ull << 30) - 1)) | (va & ((1ull << 30) - 1));
    t = dm(s, e & PA_MASK); e = t[(va >> 21) & 511];
    if (!(e & 1)) return 0;
    if (e & 0x80) return (e & PA_MASK & ~((1ull << 21) - 1)) | (va & ((1ull << 21) - 1));
    t = dm(s, e & PA_MASK); e = t[(va >> 12) & 511];
    if (!(e & 1)) return 0;
    return (e & PA_MASK) | (va & 0xfff);
}

/* attributes of the 2 MiB block at physical pa, as the kernel DMAP maps it. Every 4K page is
 * checked (code review: the old walk skipped missing pages, so a block with holes read as RAM). */
static enum mm_attr dmap_attr(void *ctx, u64 pa)
{
    struct db_state *s = ctx;
    u64 va = s->dmap + pa;
    u64 *t = dm(s, DB_RDCR(3) & PA_MASK), e = t[(va >> 39) & 511];
    if (!(e & 1)) return MM_PG_ABSENT;
    t = dm(s, e & PA_MASK); e = t[(va >> 30) & 511];
    if (!(e & 1)) return MM_PG_ABSENT;
    if (e & 0x80) return (e & 0x18) || (e & 0x1000) ? MM_PG_NOT_WB : MM_PG_WB;     /* 1G leaf */
    t = dm(s, e & PA_MASK); e = t[(va >> 21) & 511];
    if (!(e & 1)) return MM_PG_ABSENT;
    if (e & 0x80) return (e & 0x18) || (e & 0x1000) ? MM_PG_NOT_WB : MM_PG_WB;     /* 2M leaf */
    t = dm(s, e & PA_MASK);
    u32 present = 0, notwb = 0;
    for (u32 i = 0; i < 512; i++) {
        if (!(t[i] & 1)) continue;
        present++;
        if (t[i] & 0x98) notwb++;                                                  /* PCD|PWT|PAT */
    }
    if (!present) return MM_PG_ABSENT;
    if (notwb) return MM_PG_NOT_WB;
    return present == 512 ? MM_PG_WB : MM_PG_PARTIAL;
}

/* OrbisOS phys_avail (kbase + g_k.phys_avail), sanity-checked; 0 pairs = no filter */
#define PA_MAX_PAIRS 31
static u64 pav[2 * PA_MAX_PAIRS];

static u32 load_phys_avail(struct db_state *s)
{
    const u64 *t = (const u64 *)(s->kbase + g_k.phys_avail);
    u32 n = 0;
    for (; n < PA_MAX_PAIRS && (t[2 * n] | t[2 * n + 1]); n++) {
        u64 a = t[2 * n], b = t[2 * n + 1];
        if ((a | b) & 0xfff || b <= a || b > (1ull << 40) || (n && a < pav[2 * n - 1])) return 0;  /* not sane */
        pav[2 * n] = a; pav[2 * n + 1] = b;
    }
    return n >= 2 && n < PA_MAX_PAIRS ? n : 0;
}

#define RSV_MAX 256
static u32 g_rsv_drops;
static struct mm_stats g_mm_stats;                       /* for the boot-log summary line */
static struct mm_range rsv[RSV_MAX];

static void build_memmap(struct db_state *s)
{
    u32 n = 0;
    rsv[n++] = (struct mm_range){ s->boot_pa, s->boot_size };
    rsv[n++] = (struct mm_range){ s->fbo_pa, s->fbo_size };
    rsv[n++] = (struct mm_range){ s->region_pa, s->region_size };   /* UPL entry, HOBs, stack */
    rsv[n++] = (struct mm_range){ s->elf_pa, s->elf_size };         /* UPL ELF + in-place FVs */
    rsv[n++] = (struct mm_range){ DB_VRAM_BASE, DB_VRAM_SIZE };      /* VRAM carve */
    for (u64 o = 0; o < s->kblob_size; o += 0x1000) {                 /* parked APs' code + state */
        u64 pa = va2pa(s, s->kblob + o);
        if (!pa) continue;
        if (n < RSV_MAX) rsv[n++] = (struct mm_range){ pa & ~0xfffull, 0x1000 };
        else g_rsv_drops++;                                             /* shown as marker 3 yellow */
    }
    struct mm_ctx c = {
        .pav = pav, .pav_n = load_phys_avail(s), .rsv = rsv, .rsv_n = n,
        .top_mem = db_rdmsr(0xc001001a) & 0x000ffffffff00000ull,   /* H16 */
        .tom2 = db_rdmsr(0xc001001d) & 0x000ffffffff00000ull,
        .attr = dmap_attr, .actx = s,
        .hi_ram_base = DB_VRAM_BASE + DB_VRAM_SIZE, .hi_ram_end = DB_HI_RAM_END,
    };
    if (c.hi_ram_end > c.tom2) c.hi_ram_end = c.tom2;
    struct mm_stats st = mm_build(&c, &s->in);
    g_rsv_drops += st.dropped;
    g_mm_stats = st;
}

/* ---- PCI-level interrupt sources off (ECAM 0xF8000000, H12): MSI Enable / MSI-X Enable cleared,
 * INTx Disable set; Bus Master and the display engine left alone so the picture stays.
 * The GPU (00:01.0) and HDA (00:01.1) use PCI MSI (run 2); the reference does the same per
 * function on Baikal (disableMSI). Needed because DXE enables interrupts right after CpuDxe
 * (HW run 20:5x UTC: silent stop there) while the live display engine can still raise MSIs. */
static void pci_irqs_off(struct db_state *s, u32 dev, u32 fn)
{
    u64 cfg = 0xf8000000ull | (u64)dev << 15 | (u64)fn << 12;
    u32 id = r32(s, cfg);
    if (id == 0xffffffffu || id == 0) return;
    m32(s, cfg + 0x04, 0, 1u << 10);                     /* COMMAND.InterruptDisable */
    if (!(r32(s, cfg + 0x04) & (0x10u << 16))) return;   /* STATUS.CapabilitiesList */
    u32 ptr = r32(s, cfg + 0x34) & 0xfc;
    for (u32 n = 0; ptr && n < 48; n++) {
        u32 hdr = r32(s, cfg + ptr);
        if ((hdr & 0xff) == 0x05) m32(s, cfg + ptr, 1u << 16, 0);        /* MSI: Enable (ctl bit0) */
        if ((hdr & 0xff) == 0x11) m32(s, cfg + ptr, 1u << 31, 0);        /* MSI-X: Enable (ctl bit15) */
        ptr = (hdr >> 8) & 0xfc;
    }
}

/* ---- device quiesce on the boot CPU (reference order) ---- */
static void quiesce_devices(struct db_state *s)
{
    /* Same device set as e86680d9, which survived a 180 s hold (HW 23:53 UTC), and as the
     * reference loader (linux_boot.c:350-380): IOMMU off first, NO PCI Bus Master changes.
     * HW 06:08 UTC: clearing Bus Master on HDA/GbE/AHCI/SDHCI/DMAC/xHCI was followed by a
     * power-off ~10 s later (analysis/POWEROFF_RE_2026-11-20.md). The main external DMA source
     * (wifi/bt over SDIO) is now reset through OrbisOS's own wlanbt(2) before takeover (arm.c
     * db_wlanbt), exactly as the reference hook does. */
    /* HDMI audio (GPU function 00:01.1) keeps streaming after takeover (the audible hum); with the
     * IOMMU off its stream/position-buffer DMA lands on raw physical addresses. HW run 06:53 UTC froze
     * mid-print at the same CpuDxe line as 23:53/00:30/01:50. Stop it through its own registers as Linux
     * does (hda.c, tested against QEMU intel-hda in rebuild/qemu), BEFORE the IOMMU goes off.
     * Marker slot 11: green = stopped, yellow = partly, grey = no AMD audio function at 00:01.1. */
    {
        u64 cfg = 0xf8000000ull | (1ull << 15) | (1ull << 12);
        u32 id = r32(s, cfg), cls = r32(s, cfg + 0x08) >> 16, b0 = r32(s, cfg + 0x10);
        u64 bar = b0 & ~0xfull;
        if ((b0 & 6) == 4) bar |= (u64)r32(s, cfg + 0x14) << 32;           /* 64-bit BAR */
        if ((id & 0xffff) == 0x1002 && cls == 0x0403 && !(b0 & 1) && bar && bar < (1ull << 40)) {
            struct hda_stop_result hr;
            int rc = hda_stop((volatile u8 *)dm(s, bar), &hr, delay_us_cb, s);
            marker(s, 11, rc == 0 ? C_GREEN : C_YELL);
        } else {
            marker(s, 11, C_GREY);
        }
    }
    *(volatile u64 *)dm(s, HW_IOMMU_CTRL) &= ~1ull;            /* IOMMU off (E2) */
    pci_irqs_off(s, 0x01, 0);                                  /* GPU  (MSI)          */
    pci_irqs_off(s, 0x01, 1);                                  /* HDA  (MSI)          */
    for (u32 f = 0; f < 8; f++) pci_irqs_off(s, 0x14, f);      /* Aeolia 14.0-14.7    */
    for (u32 i = 0; i < 8; i++) w32(s, HW_MSI_MASK0 + 4 * i, 0);   /* Aeolia MSIs off (H2b) */
    w64(s, HW_HPET + 0x010, 0);                                /* HPET config: stop (E3) */
    for (u32 t = 0; t < 4; t++) w64(s, HW_HPET + 0x100 + 0x20 * t, 0);
    /* GPU command engines halt + GRBM/SRBM soft reset (reference values), 32-bit accesses: the
     * reference's 64-bit stores also zero the neighbouring register. DCE (display) untouched. */
    w32(s, HW_GPU_MMIO + 0x86d8, 0x15000000);   /* CP_ME_CNTL: halt */
    w32(s, HW_GPU_MMIO + 0x8234, 0x50000000);   /* CP_MEC_CNTL: halt */
    w32(s, HW_GPU_MMIO + 0xd048, 1);            /* SDMA0 halt */
    w32(s, HW_GPU_MMIO + 0xd848, 1);            /* SDMA1 halt */
    w32(s, HW_GPU_MMIO + 0xc300, 0);            /* RLC halt */
    m32(s, HW_GPU_MMIO + 0xc1a8, 0x180000, 0);  /* CP_INT_CNTL_RING0 */
    /* Wait (bounded, 100 ms) for the graphics block to go idle before the soft reset, as OrbisOS's
     * gc_suspend_resume.c does (it waits for the "GPU GUI Idle" interrupt) and as amdgpu does.
     * GRBM_STATUS = mmGRBM_STATUS 0x2004 -> byte 0x8010, GUI_ACTIVE bit 31
     * (strawberry gfx_7_2_d.h:787, gfx_7_2_sh_mask.h:4795). analysis/GRAPHICS_SDK_NOTES_2026-11-20.md */
    for (u32 ms = 0; ms < 100 && (r32(s, HW_GPU_MMIO + 0x8010) & 0x80000000u); ms++)
        delay_ms(s, 1);
    m32(s, HW_GPU_MMIO + 0x8020, 0, 0x30005);   /* GRBM_SOFT_RESET: CP/RLC/GFX/CPF */
    delay_ms(s, 1);
    m32(s, HW_GPU_MMIO + 0x8020, 0x30005, 0);
    m32(s, HW_GPU_MMIO + 0x0e60, 0, 0x00100140); /* SRBM_SOFT_RESET: DMA1/GRBM/DMA (not DC) */
    delay_ms(s, 1);
    m32(s, HW_GPU_MMIO + 0x0e60, 0x00100140, 0);
}

/* ---- page tables + GDT in the boot block, then switch ---- */
static void switch_tables(struct db_state *s)
{
    u64 *pml4 = dm(s, s->boot_pa), *pdpt = dm(s, s->boot_pa + 0x1000);
    u64 *gdt = dm(s, s->boot_pa + 0x2000);
    const u64 *cur = dm(s, DB_RDCR(3) & PA_MASK);
    for (u32 i = 0; i < 512; i++) {
        pdpt[i] = ((u64)i << 30) | 0x83;                 /* identity 1 GiB, P|RW|PS */
        pml4[i] = i >= 256 ? cur[i] : 0;                 /* keep the kernel half (this code, DMAP) */
    }
    pml4[0] = (s->boot_pa + 0x1000) | 0x03;
    gdt[0] = 0; gdt[1] = 0;
    gdt[2] = 0x00af9a000000ffffull;                      /* 0x10: 64-bit code */
    gdt[3] = 0x00cf92000000ffffull;                      /* 0x18: data */
    struct __attribute__((packed)) { u16 limit; u64 base; } gdtr = { 4 * 8 - 1, s->boot_pa + 0x2000 };
    struct __attribute__((packed)) { u16 limit; u64 base; } idtr = { 0, 0 };

    DB_WRCR(4, DB_RDCR(4) & ~(1ull << 7));               /* PGE off: flush global TLB entries */
    DB_WRCR(3, s->boot_pa);
    __asm__ volatile(
        "lgdt %0\n\t"
        "lidt %1\n\t"
        "mov $0x18, %%eax\n\t"
        "mov %%eax, %%ds\n\t mov %%eax, %%es\n\t mov %%eax, %%ss\n\t"
        "mov %%eax, %%fs\n\t mov %%eax, %%gs\n\t"
        "pushq $0x10\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        :: "m"(gdtr), "m"(idtr) : "rax", "memory");
}

/* Framebuffer debug console descriptor for Ps4FbSerialPortLib (Ps4Pkg/Include/Ps4FbConsole.h):
 * fixed physical 0x8000, inside the low 1 MiB the memory map reserves. */
struct ps4_fbcon { u32 magic, version; u64 fb_base; u32 width, height, pitch, col, row, rsvd; u64 tsc0, tsc_hz;
                   u64 log_base; u32 log_size, log_rsvd; };
struct ps4_log_hdr { u32 magic, size; u64 written; };   /* = PS4_LOG_HEADER */
_Static_assert(sizeof(struct ps4_fbcon) == 72 && __builtin_offsetof(struct ps4_fbcon, tsc0) == 40 &&
               __builtin_offsetof(struct ps4_fbcon, log_base) == 56,
               "must match Ps4Pkg/Include/Ps4FbConsole.h PS4_FBCON (version 3)");
_Static_assert(sizeof(struct ps4_log_hdr) == 16, "must match PS4_LOG_HEADER");
static void fbcon_publish(struct db_state *s)
{
    struct ps4_fbcon *c = dm(s, 0x8000);
    if (!s->fb_base) return;
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    c->log_base = DB_LOG_BASE; c->log_size = (u32)DB_LOG_SIZE; c->log_rsvd = 0;   /* header set after switch_tables */
    c->version = 3; c->fb_base = s->fb_base; c->width = s->fb_w; c->height = s->fb_h;
    c->pitch = s->fb_pitch; c->col = 0; c->row = 7; c->rsvd = 0;
    c->tsc0 = ((u64)hi << 32) | lo; c->tsc_hz = s->tsc_hz;      /* EDK II lines show [s.ms] since now */
    __asm__ volatile("" ::: "memory");
    c->magic = 0x43463450u;                              /* "P4FC" last */
}

/* ---- boot-log ring: header + a stage summary line; EDK II appends after this ---- */
static void log_put(struct ps4_log_hdr *h, const char *t)
{
    u8 *d = (u8 *)(h + 1);
    for (; *t; t++) d[h->written++ % h->size] = (u8)*t;
}
static void log_hex(struct ps4_log_hdr *h, u64 v)
{
    char b[19]; b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 16; i++) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
    b[18] = 0; log_put(h, b);
}
static void log_init(struct db_state *s)
{
    struct ps4_log_hdr *h = (struct ps4_log_hdr *)DB_LOG_BASE;          /* identity-mapped */
    h->size = (u32)DB_LOG_SIZE; h->written = 0;
    __asm__ volatile("" ::: "memory");
    h->magic = 0x474F4C50u;                                             /* "PLOG" */
    log_put(h, "directboot stage (rebuild2): memmap entries "); log_hex(h, s->in.mm_count);
    log_put(h, " RAM bytes "); log_hex(h, g_mm_stats.ram_bytes);
    log_put(h, " reserve drops "); log_hex(h, g_rsv_drops);
    log_put(h, " VRAM carve "); log_hex(h, DB_VRAM_BASE); log_put(h, "+"); log_hex(h, DB_VRAM_SIZE);
    log_put(h, " TSC Hz "); log_hex(h, s->tsc_hz); log_put(h, "\n");
}

static void upl_progress(void *ctx, u32 step)            /* purple squares 5..8 inside upl_load */
{
    marker((struct db_state *)ctx, 4 + step, 0xa040ff);
}

static void halt_forever(void)
{
    for (;;) __asm__ volatile("cli; hlt");
}

static void takeover(struct db_state *s)
{
    display_takeover(s);                                 /* before any GPU engine reset */
    fbcon_publish(s);
    marker(s, 0, s->halted == DB_NCPU - 1 ? C_WHITE : C_YELL);
    marker(s, 1, s->fbo_latched ? C_GREEN : C_YELL);
    quiesce_devices(s);
    configure_vram(s);                                   /* GPU engines halted above */
    marker(s, 2, C_GREEN);
    build_memmap(s);
    marker(s, 3, g_rsv_drops ? C_YELL : C_GREEN);

    struct upl_inputs *in = &s->in;
    in->elf = (const u8 *)s->elf_pa;                     /* identity after the switch */
    in->elf_size = s->elf_size;
    in->region_phys = s->region_pa; in->region_size = s->region_size;
    in->fb_base = s->fb_base; in->fb_size = (u64)s->fb_pitch * s->fb_h;
    in->fb_w = s->fb_w; in->fb_h = s->fb_h; in->fb_stride = s->fb_pitch; in->fb_bpp = 32;
    in->rsdp = 0xe0000;                                  /* Sony RSDP (run 2) */
    ps4_directboot_info *pi = &s->info;
    pi->version = PS4_DIRECTBOOT_INFO_VERSION; pi->size = sizeof *pi;
    pi->unix_time = s->unix_time; pi->tsc_hz = s->tsc_hz; pi->fb_base = s->fb_base;
    pi->fb_width = s->fb_w; pi->fb_height = s->fb_h; pi->fb_pitch_bytes = s->fb_pitch;
    pi->fb_crtc = s->fb_crtc; pi->kbase = s->kbase;
    pi->boot_block = s->boot_pa; pi->boot_block_size = s->boot_size; pi->halted_aps = s->halted;
    in->ps4 = pi;
    in->progress = upl_progress; in->progress_ctx = s;

    switch_tables(s);
    log_init(s);                                         /* identity map now: the ring is reachable */
    __asm__ volatile("clts");                            /* CR0.TS was set (FreeBSD lazy FPU, run 2
                                                          * CR0 0x8005003b): EDK II's first SSE op
                                                          * would #NM with no IDT -> triple fault */
    marker(s, 4, C_BLUE);
#ifndef DB_HOLD_SECONDS
#define DB_HOLD_SECONDS 30     /* 06:08 UTC: power-off came ~10 s into the hold; 30 s still shows a ~10 s killer */
#endif
    /* Diagnostic hold. 23:34 UTC: a 30 s hold let EDK II reach BDS, then the console shut down.
     * 180 s here, everything quiesced, interrupts off, one core in a pause loop: if the console
     * shuts down DURING this hold, the cause is outside EDK II (heat/syscon); if it survives,
     * EDK II triggers it. Two rows of 90 small squares (every 10th yellow) above the text. */
    for (u32 sec = 0; sec < DB_HOLD_SECONDS; sec++) {
        delay_ms(s, 1000);
        if (s->fb_base && s->fb_w > 48 + 90 * 14) {
            u8 *fb = dm(s, s->fb_base);
            u32 x0 = 48 + (sec % 90) * 14, y0 = 110 + (sec / 90) * 11;
            u32 c = (sec % 10 == 9) ? 0xffffd000u : 0xffffffffu;
            for (u32 y = y0; y < y0 + 8; y++) {
                u32 *row = (u32 *)(fb + (u64)y * s->fb_pitch);
                for (u32 x = x0; x < x0 + 10; x++) row[x] = c;
            }
            __asm__ volatile("sfence" ::: "memory");
        }
    }
    const char *err = upl_load(in);                      /* does not return on success */
    (void)err;
    marker(s, 9, C_RED);                                 /* upl_load returned an error */
    halt_forever();
}

/* every CPU, interrupts off (smp_rendezvous action) */
static void db_gate(void *arg)
{
    struct db_state *s = arg;
    __asm__ volatile("cli");
    DB_WRCR(0, DB_RDCR(0) & ~(1ull << 16));
    __atomic_fetch_add(&s->entered, 1, __ATOMIC_SEQ_CST);
    lapic_quiet(s);
    fb_mtrr(s);
    vram_mtrr(s);
    if (__atomic_exchange_n(&s->cpu_lock, 1, __ATOMIC_SEQ_CST) != 0) {
        __atomic_fetch_add(&s->halted, 1, __ATOMIC_SEQ_CST);
        halt_forever();
    }
    for (u64 spin = 0; s->halted < DB_NCPU - 1 && spin < 300000000ull; spin++)
        __asm__ volatile("pause");
    takeover(s);
}

int db_hook(u8 *icc_msg)
{
    (void)icc_msg;
    struct db_state *s = &g_db;                          /* the kernel copy's own state */
    if (s->magic != DB_STATE_MAGIC) return 0;
    if (__atomic_fetch_add(&s->hook_hits, 1, __ATOMIC_SEQ_CST) != 0) {
        /* a second caller (another CPU in icc_query_nowait): wait with interrupts ON, so the
         * rendezvous IPI still reaches it and it parks in db_gate like every other CPU */
        for (;;) __asm__ volatile("sti; pause" ::: "memory");
    }
    /* live scanout if a pipe is still on; else keep what db_arm saw while OrbisOS drew */
    u64 fb0 = s->fb_base; u32 w0 = s->fb_w, h0 = s->fb_h, p0 = s->fb_pitch, c0 = s->fb_crtc;
    s->fb_base = 0; s->fb_w = s->fb_h = 0;
    db_find_scanout(s);
    if (!s->fb_base) { s->fb_base = fb0; s->fb_w = w0; s->fb_h = h0; s->fb_pitch = p0; s->fb_crtc = c0; }
    marker(s, 0, C_BLUE);
    /* no wlanbt(2): it talks ICC and could re-enter this hook on the boot CPU */
    smp_fn_t nb = (smp_fn_t)(s->kbase + g_k.smp_nobarrier);
    ((smp_rdv_t)(s->kbase + g_k.smp_rdv))(nb, db_gate, nb, s);
    halt_forever();
    return 0;
}
