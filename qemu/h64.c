/* h64 - 64-bit QEMU harness for the directboot handoff. Mirrors hook.c's takeover tail:
 * fbcon descriptor v2 at 0x8000, memory map from src/directboot/memmap.c (same rules and a
 * PS4-shaped phys_avail filter), struct upl_inputs as the stage fills it, then src/upl_load.c,
 * which loads out/upl-ps4.elf (loaded by QEMU at ELF_ADDR) and jumps to EDK II.
 * What QEMU cannot show: Aeolia, the missing 8259/IOAPIC (LINT0), device quiesce, OrbisOS. */
#include "../src/directboot/memmap.h"
#include "hinfo.h"
#include "../src/directboot/hda.h"

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static void dbg(const char *s) { while (*s) outb(0xE9, (u8)*s++); }
static void dbgx(const char *k, u64 v)
{
    static const char hx[] = "0123456789abcdef";
    char b[20]; int n = 0;
    dbg(k); b[n++] = '0'; b[n++] = 'x';
    for (int i = 60; i >= 0; i -= 4) b[n++] = hx[(v >> i) & 15];
    b[n++] = '\n'; b[n] = 0; dbg(b);
}
static __attribute__((noreturn)) void stop(const char *why) { dbg(why); dbg("\n"); for (;;) __asm__ volatile("cli; hlt"); }

void *memset(void *d, int c, size_t n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }

static const struct hinfo *H;

static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static u32 pci_rd(u32 d, u32 f, u32 o) { outl(0xCF8, 0x80000000u | (d << 11) | (f << 8) | (o & 0xFC)); return inl(0xCFC); }
static void pci_wr(u32 d, u32 f, u32 o, u32 v) { outl(0xCF8, 0x80000000u | (d << 11) | (f << 8) | (o & 0xFC)); outl(0xCFC, v); }
static u64 g_tsc_hz;
static void udelay_tsc(void *ctx, u32 us)
{
    (void)ctx; u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    u64 t0 = ((u64)hi << 32) | lo, need = (g_tsc_hz ? g_tsc_hz : 2000000000ull) / 1000000 * us;
    for (;;) { __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); if ((((u64)hi << 32) | lo) - t0 >= need) break; }
}

/* Exercise src/directboot/hda.c on QEMU's intel-hda: bring the controller up, start CORB/RIRB,
 * the DMA position buffer and one output stream, then stop everything with hda_stop(). */
static void hda_selftest(void)
{
    u32 dev = 32;
    for (u32 d = 0; d < 32; d++)
        if ((pci_rd(d, 0, 0) & 0xFFFF) != 0xFFFF && (pci_rd(d, 0, 8) >> 16) == 0x0403) { dev = d; break; }
    if (dev == 32) { dbg("h64: hda selftest: no HDA device (run.sh adds -device intel-hda)\n"); return; }
    pci_wr(dev, 0, 4, pci_rd(dev, 0, 4) | 0x6);                 /* memory decode + bus master */
    volatile u8 *m = (volatile u8 *)(u64)(pci_rd(dev, 0, 0x10) & ~0xFu);
    dbgx("h64: hda bar0 ", (u64)m);
    *(volatile u32 *)(m + 0x08) = 1;                             /* GCTL.CRST = 1: leave reset */
    for (u32 t = 0; t < 1000 && !(*(volatile u32 *)(m + 0x08) & 1); t++) udelay_tsc(0, 100);
    u64 scratch = 0x00F00000ull;                                  /* RAM, stopped before upl_load */
    for (u32 i = 0; i < 0x4000 / 8; i++) ((volatile u64 *)scratch)[i] = 0;
    *(volatile u32 *)(m + 0x40) = (u32)scratch;          *(volatile u32 *)(m + 0x44) = 0;   /* CORB */
    *(volatile u32 *)(m + 0x50) = (u32)scratch + 0x400;  *(volatile u32 *)(m + 0x54) = 0;   /* RIRB */
    *(volatile u8 *)(m + 0x5c) = 0x2;                             /* RIRBCTL.DMA_EN */
    *(volatile u8 *)(m + 0x4c) = 0x2;                             /* CORBCTL.RUN */
    *(volatile u32 *)(m + 0x70) = (u32)scratch + 0x800 | 1;       /* DPLBASE + enable */
    u32 gcap = *(volatile u32 *)(m) & 0xffff, iss = (gcap >> 8) & 15;
    u32 sd = 0x80 + 0x20 * iss;                                   /* first output stream */
    volatile u64 *bdl = (volatile u64 *)(scratch + 0x1000);
    bdl[0] = scratch + 0x2000; bdl[1] = 0x1000; bdl[2] = scratch + 0x3000; bdl[3] = 0x1000;
    *(volatile u32 *)(m + sd + 0x08) = 0x2000;                    /* CBL */
    *(volatile u16 *)(m + sd + 0x0c) = 1;                         /* LVI */
    *(volatile u16 *)(m + sd + 0x12) = 0x4011;                    /* 48 kHz, 16 bit, stereo */
    *(volatile u32 *)(m + sd + 0x18) = (u32)(scratch + 0x1000); *(volatile u32 *)(m + sd + 0x1c) = 0;
    *(volatile u32 *)(m + sd) = (1u << 20) | 0x2;                 /* stream tag 1, RUN */
    udelay_tsc(0, 2000);
    struct hda_stop_result r;
    int rc = hda_stop(m, &r, udelay_tsc, 0);
    dbgx("h64: hda gcap ", r.gcap); dbgx("h64: hda streams ", r.streams);
    dbgx("h64: hda running before ", r.running_before); dbgx("h64: hda running after ", r.running_after);
    dbgx("h64: hda corb|rirb<<8 before ", r.corb_rirb_before); dbgx("h64: hda corb|rirb<<8 after ", r.corb_rirb_after);
    dbgx("h64: hda dplbase before ", r.dplbase_before);
    dbgx("h64: hda gctl before ", r.gctl_before); dbgx("h64: hda gctl after ", r.gctl_after);
    dbg(rc == 0 && r.running_before && (r.corb_rirb_before & 0x0202) == 0x0202 && (r.dplbase_before & 1)
        ? "h64: HDA SELFTEST PASS\n" : "h64: HDA SELFTEST FAIL\n");
}

/* same contract as hook.c dmap_attr, from the multiboot RAM map: WB if the whole 2 MiB block is
 * e820 RAM, ABSENT otherwise (QEMU has no UC carve-outs inside RAM) */
static enum mm_attr e820_attr(void *ctx, u64 pa)
{
    (void)ctx;
    for (u32 i = 0; i < H->n_e820; i++)
        if (H->e820[i].type == 1 && pa >= H->e820[i].base && pa + (2ull << 20) <= H->e820[i].base + H->e820[i].len)
            return MM_PG_WB;
    return MM_PG_ABSENT;
}

/* stage-shaped phys_avail inside QEMU's 2 GiB: the two big 9.00 ranges (dump kbase+0x2216a80) */
static const u64 pav[] = { 0x700000, 0x9400000, 0x14928000, 0x57738000 };

static struct upl_inputs in;
static ps4_directboot_info info;
static upl_pci_root_bridge rb;

struct fbcon { u32 magic, version; u64 fb_base; u32 width, height, pitch, col, row, rsvd; u64 tsc0, tsc_hz;
               u64 log_base; u32 log_size, log_rsvd; };              /* v3 = Ps4FbConsole.h */
#define H64_LOG_BASE 0x7f000000ull                                 /* 1 MiB ring + header page, reserved */
#define H64_LOG_SIZE 0x100000ull
_Static_assert(sizeof(struct fbcon) == 72 && __builtin_offsetof(struct fbcon, log_base) == 56, "Ps4FbConsole.h PS4_FBCON v3");

/* STALE_LAPIC=1 (run.sh): reproduce the LAPIC state the PS4 takeover can leave behind. The hook
 * (icc_query_nowait) can run inside an OrbisOS interrupt handler, so that vector stays in service
 * (no EOI ever) and lower vectors stay pending. Here: vector 0x40 in service (its handler returns
 * without EOI) and 0x30 pending, then the IDTR goes back to {0,0} as the stage leaves it. */
#define STALE_FLAG_ADDR  0x00ff0000ull
#define STALE_MAGIC      0x4c415453u                     /* "STAL" */
#define LAPIC(o)         (*(volatile u32 *)(0xfee00000ull + (o)))
struct gate { u16 lo, sel; u8 ist, attr; u16 mid; u32 hi, rsvd; } __attribute__((packed));
static struct gate stale_idt[256] __attribute__((aligned(16)));
__attribute__((naked)) static void stale_isr(void) { __asm__ volatile("iretq"); }
static void lapic_dump(const char *tag)
{
    dbg(tag);
    dbgx("  SVR ", LAPIC(0xf0)); dbgx("  TPR ", LAPIC(0x80)); dbgx("  PPR ", LAPIC(0xa0));
    for (u32 i = 0; i < 8; i++) { if (LAPIC(0x100 + 0x10 * i)) dbgx("  ISR word ", ((u64)i << 32) | LAPIC(0x100 + 0x10 * i)); }
    for (u32 i = 0; i < 8; i++) { if (LAPIC(0x200 + 0x10 * i)) dbgx("  IRR word ", ((u64)i << 32) | LAPIC(0x200 + 0x10 * i)); }
}
static void lapic_leave_stale(void)
{
    u16 cs; __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    u64 a = (u64)stale_isr;
    stale_idt[0x40] = (struct gate){ (u16)a, cs, 0, 0x8e, (u16)(a >> 16), (u32)(a >> 32), 0 };
    struct { u16 lim; u64 base; } __attribute__((packed)) idtr = { sizeof stale_idt - 1, (u64)stale_idt }, null = { 0, 0 };
    LAPIC(0xf0) |= 0x100;                                 /* software enable */
    LAPIC(0x80) = 0;
    __asm__ volatile("lidt %0" : : "m"(idtr));
    LAPIC(0x310) = 0; LAPIC(0x300) = 0x40 | (1u << 18);   /* self IPI, fixed, vector 0x40 */
    __asm__ volatile("sti; nop; nop; nop; nop; cli" ::: "memory");
    udelay_tsc(0, 1000);
    LAPIC(0x310) = 0; LAPIC(0x300) = 0x30 | (1u << 18);   /* stays pending: IF=0 and below 0x40 */
    udelay_tsc(0, 1000);
    __asm__ volatile("lidt %0" : : "m"(null));
    lapic_dump("h64: STALE_LAPIC left behind:\n");
}

static void progress(void *ctx, u32 step) { (void)ctx; dbgx("h64: upl_load step ", step); }

/* The PS4's xHCI/AHCI/SDHCI functions arrive with memory decode + bus master on (OrbisOS used them),
 * and the firmware never writes PCI config. SeaBIOS may leave bus master off, so set the same state
 * here for QEMU's xHCI / AHCI / SDHCI functions (by class code) before the handoff. */
static void io_devices_as_os_left(void)
{
    for (u32 d = 0; d < 32; d++)
        for (u32 f = 0; f < 8; f++) {
            u32 id = pci_rd(d, f, 0);
            if ((id & 0xffff) == 0xffff || (id & 0xffff) == 0) { if (!f) break; continue; }
            u32 cls = pci_rd(d, f, 8) >> 8;
            if (cls == 0x0c0330 || cls == 0x010601 || cls == 0x080501 || (cls >> 16) == 0x02) {   /* + NICs (NET_TEST) */
                pci_wr(d, f, 4, (pci_rd(d, f, 4) & 0xffff) | 6);
                dbgx("h64: io device mem+bme on, dev.fn ", d << 8 | f);
            }
            if (!f && !(pci_rd(d, 0, 0x0c) & (0x80u << 16))) break;
        }
}

__attribute__((section(".text.start"))) void h64_main(void)
{
    H = (const struct hinfo *)(u64)HINFO_ADDR;
    if (H->magic != HINFO_MAGIC) stop("h64: no hinfo");
    dbg("h64: start\n");
    dbgx("h64: fb ", H->fb); dbgx("h64: rsdp ", H->rsdp); dbgx("h64: upl size ", H->upl_size);
    dbgx("h64: pci mem lo ", H->mem_lo);
    if (!H->fb || !H->upl_size) stop("h64: missing fb or UPL");
    g_tsc_hz = H->tsc_hz;
    hda_selftest();
    io_devices_as_os_left();

    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    u64 tsc0 = ((u64)hi << 32) | lo;

    /* black screen, then the fb console descriptor exactly as hook.c fbcon_publish writes it */
    for (u32 y = 0; y < H->fb_h; y++) {
        volatile u32 *row = (volatile u32 *)(H->fb + (u64)y * H->fb_pitch);
        for (u32 x = 0; x < H->fb_w; x++) row[x] = 0xff000000u;
    }
    volatile struct fbcon *c = (volatile struct fbcon *)0x8000ull;
    { volatile u32 *lh = (volatile u32 *)H64_LOG_BASE; lh[1] = (u32)H64_LOG_SIZE; *(volatile u64 *)(lh + 2) = 0; lh[0] = 0x474F4C50u; }
    c->log_base = H64_LOG_BASE; c->log_size = (u32)H64_LOG_SIZE; c->log_rsvd = 0;
    c->version = 3; c->fb_base = H->fb; c->width = H->fb_w; c->height = H->fb_h;
    c->pitch = H->fb_pitch; c->col = 0; c->row = 7; c->tsc0 = tsc0; c->tsc_hz = H->tsc_hz;
    c->rsvd = 1;                            /* PS4_FBCON_FLAG_DEBUGCON: EDK II text also to debug.log */
    c->magic = 0x43463450u;

    /* memory map: the stage's builder, reserving what the stage reserves */
    struct mm_range rsv[] = {
        { ELF_ADDR, H->upl_size }, { REGION_ADDR, REGION_SIZE }, { BOOT_ADDR, 0x10000 }, { H->fb, (u64)H->fb_pitch * H->fb_h },
        { 0x100000000ull, 0x80000000ull },     /* 2 GiB VRAM carve, as the stage (db.h DB_VRAM_*) */
        { H64_LOG_BASE, H64_LOG_SIZE + 0x1000 },/* boot-log ring */
    };
    u64 top = 0;
    for (u32 i = 0; i < H->n_e820; i++)
        if (H->e820[i].type == 1 && H->e820[i].base + H->e820[i].len <= (1ull << 32) &&
            H->e820[i].base + H->e820[i].len > top) top = H->e820[i].base + H->e820[i].len;
    u64 hiram = 0;                              /* QEMU -m 8G: q35 puts 6 GiB at 4 GiB, like the PS4 (TOM2) */
    for (u32 i = 0; i < H->n_e820; i++)
        if (H->e820[i].type == 1 && H->e820[i].base >= (1ull << 32) && H->e820[i].base + H->e820[i].len > hiram)
            hiram = H->e820[i].base + H->e820[i].len;
    struct mm_ctx m = { pav, sizeof pav / 16, rsv, sizeof rsv / sizeof rsv[0],
                        top & ~((2ull << 20) - 1), hiram & ~((2ull << 20) - 1), e820_attr, 0, 0, 0 };
    if (hiram > 0x180000000ull) {               /* release high RAM above the carve, as the stage */
        m.hi_ram_base = 0x180000000ull;
        m.hi_ram_end  = hiram < 0x274000000ull ? (hiram & ~((2ull << 20) - 1)) : 0x274000000ull;
        dbgx("h64: high RAM released up to ", m.hi_ram_end);
    }
    struct mm_stats st = mm_build(&m, &in);
    dbgx("h64: memmap entries ", in.mm_count); dbgx("h64: RAM bytes ", st.ram_bytes); dbgx("h64: dropped ", st.dropped);

    info.version = PS4_DIRECTBOOT_INFO_VERSION; info.size = sizeof info;
    info.tsc_hz = H->tsc_hz; info.fb_base = H->fb; info.fb_width = H->fb_w; info.fb_height = H->fb_h;
    info.fb_pitch_bytes = H->fb_pitch; info.boot_block = BOOT_ADDR; info.boot_block_size = 0x10000; info.flags = H->rsvd;

    /* q35: bus 0, BARs as SeaBIOS assigned them, MMIO up to below the IOAPIC */
    rb.allocation_attributes = 1 | 2;
    rb.bus = (upl_pci_aperture){ 0, 0, 0 };
    rb.io = (upl_pci_aperture){ 0x1000, 0xffff, 0 };
    rb.mem = (upl_pci_aperture){ H->mem_lo, H->mem_hi, 0 };
    rb.mem_above_4g = (upl_pci_aperture){ ~0ull, 0, 0 };
    rb.pmem = (upl_pci_aperture){ ~0ull, 0, 0 };
    rb.pmem_above_4g = (upl_pci_aperture){ ~0ull, 0, 0 };
    rb.hid = 0x0A0341D0u;

    in.elf = (const u8 *)ELF_ADDR; in.elf_size = H->upl_size;
    in.region_phys = REGION_ADDR; in.region_size = REGION_SIZE;
    in.fb_base = H->fb; in.fb_size = (u64)H->fb_pitch * H->fb_h;
    in.fb_w = H->fb_w; in.fb_h = H->fb_h; in.fb_stride = H->fb_pitch; in.fb_bpp = 32;
    in.rsdp = H->rsdp; in.ps4 = &info; in.pci_rb = &rb;
    in.progress = progress; in.progress_ctx = 0;
    if (*(volatile u32 *)STALE_FLAG_ADDR == STALE_MAGIC) lapic_leave_stale();
    dbg("h64: upl_load\n");
    const char *err = upl_load(&in);
    stop(err ? err : "h64: upl_load returned");
}
