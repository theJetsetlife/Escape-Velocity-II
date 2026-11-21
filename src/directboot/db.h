/* Directboot stage — shared definitions.
 *
 * Architecture = the working ps4-linux-loader chain (REFERENCE_CHAIN.md) with EDK II UPL in
 * place of bzImage:
 *   userland (WebKit, GoldHEN slot) -> fetch upl-ps4.elf from the PC -> kexec(db_arm)
 *   db_arm (ring0): gates, kmem_alloc RWX patches, copy this blob into kernel memory,
 *                   stage the ELF + regions with kmem_alloc_contig, hook icc_query_nowait
 *   user restarts the console -> shutdown_final -> icc_query_nowait -> db_hook (kernel copy)
 *   db_hook: smp_rendezvous(db_gate): quiesce every CPU, one CPU continues, IOMMU/MSI/HPET/GPU
 *            engines off (display kept), memory map, page tables + GDT, upl_load -> EDK II.
 *
 * Every offset is 9.00-only and tied to LEDGER evidence; db_arm re-checks the code bytes at
 * runtime and refuses to patch anything that does not match.
 */
#ifndef DB_H
#define DB_H

#include "../diag/rec.h"
#include "../upl_load.h"

/* ---- 9.00 kernel offsets (LEDGER) ---- */
#define K_XFAST          0x1c0       /* A5 */
#define K_GADGET         0x4c7ad     /* A1 */
#define K_PRINTF         0x0b7a30    /* REFERENCE_CHAIN 4, prologue verified */
#define K_KMEM_ALLOC     0x37be70    /* B2/REFERENCE_CHAIN 3 */
#define K_KMEM_PATCH1    0x37bf3c    /* imm32 of `mov $prot,%r9d` @0x37bf3a */
#define K_KMEM_PATCH2    0x37bf44    /* imm8 of `push $prot` @0x37bf43 */
#define K_PSTATE         0x29a970    /* imm32 of `mov $1,%edi` @0x29a96f before set_pstate */
#define K_KERNEL_MAP     0x2268d48   /* vm_map_t kernel_map (pointer variable) */
#define K_KMEM_CONTIG    0x270880    /* kmem_alloc_contig */
#define K_PMAP_EXTRACT   0x12d050    /* pmap_extract(pmap, va) */
#define K_KERNEL_PMAP    0x1b904b0   /* kernel_pmap_store */
#define K_ICC_NOWAIT     0x2e1760    /* D1/D3 */
#define K_SMP_RDV        0x432bf0    /* smp_rendezvous */
#define K_SMP_NOBARRIER  0x432a00    /* smp_no_rendevous_barrier (nullsub) */
#define K_WLANBT         0x180860
#define K_KERN_REBOOT    0x29a380    /* kern_reboot(int howto); magic.h + prologue verified */
#define K_DMPML4I        0x1b904a4   /* A6 */
#define K_DMPDPI         0x1b904a8
#define K_PHYS_AVAIL     0x2216a80   /* phys_avail[] (hammer_time lea @+0x314262; dump-verified, k0probe README) */

/* ---- hardware (LEDGER H*, REFERENCE_CHAIN) ---- */
#define HW_GLUE_BAR4     0xd0200000ull       /* H14 */
#define HW_MSI_MASK0     (HW_GLUE_BAR4 + 0x1c844c)   /* H2b: 8 words */
#define HW_HPET          (HW_GLUE_BAR4 + 0x182000)   /* reference; E3 */
#define HW_IOMMU_CTRL    0xfc000018ull       /* E2 */
#define HW_GPU_MMIO      0xe4800000ull       /* H14 BAR5 */
/* 2 GiB VRAM carve at the start of high DRAM (PS4 Linux loader linux_boot.c:24-27,
 * configure_vram :111-137); system RAM above it up to the OrbisOS kernel/device-shared ranges. */
#define DB_VRAM_BASE     0x100000000ull
#define DB_VRAM_SIZE     0x80000000ull
#define DB_HI_RAM_END    0x274000000ull      /* run 3: 0x274000000.. = kernel RAM / UC device-shared */
/* 1 MiB boot-log ring = last MiB of the VRAM carve (reserved for EDK II and the OS).
 * Not in the OrbisOS DMAP: initialized after switch_tables (identity map). */
#define DB_LOG_SIZE      0x100000ull
#define DB_LOG_BASE      (DB_VRAM_BASE + DB_VRAM_SIZE - DB_LOG_SIZE - 0x1000)   /* header page + data */
#define HW_LAPIC         0xfee00000ull       /* H3 */

#define DB_STATE_MAGIC   0x5441545342444244ull   /* "DBDBSTAT" */
#define DB_NCPU          8                       /* E1 */

/* Lives in the blob's .data, so it is copied into kernel memory together with the code. */
struct db_state {
    u64 magic;
    u64 kbase, dmap;
    u64 kblob, kblob_size;             /* kernel copy of this whole blob */
    u64 elf_pa, elf_size;              /* staged upl-ps4.elf (contiguous) */
    u64 region_pa, region_size;        /* UPL load region (contiguous, WB) */
    u64 boot_pa, boot_size;            /* GDT + page tables (reserved for EDK II) */
    u64 fbo_pa, fbo_size;              /* our own linear framebuffer (reserved for EDK II) */
#define DB_HEAP_MAX    16
#define DB_HEAP_CHUNK  (16ull << 20)           /* first chunk: holds the UPL entry's 12 MiB block */
#define DB_HEAP_SMALL  (8ull << 20)
#define DB_HEAP_TOTAL  (128ull << 20)
    u64 heap_pa[DB_HEAP_MAX];          /* UEFI heap: the ONLY memory EDK II gets as RAM */
    u64 heap_sz[DB_HEAP_MAX];
    u32 heap_n;
    u32 fbo_latched;                   /* CRTC reported our surface in use */
    u64 unix_time, tsc_hz;
    /* filled by the hook */
    u64 fb_base; u32 fb_w, fb_h, fb_pitch, fb_crtc;
    volatile u32 cpu_lock, halted, entered;
    u32 hook_hits;
    struct upl_inputs in;              /* built by the boot CPU just before the jump */
    ps4_directboot_info info;
};

extern struct db_state g_db;
extern char _start[], __blob_end[];

/* userland -> ring0 arm call */
struct db_arm_args {
    u64 magic;
    struct diag_ctx *log;              /* record buffer (mlock'd) */
    const u8 *elf; u64 elf_size;       /* user copy of upl-ps4.elf */
    u64 unix_time, tsc_hz;
    s64 result;                        /* 0 = armed, <0 = step that failed */
    /* staged arm: one syscall per step, flushed to the PC in between (HANDOFF_AUDIT_2 #4) */
    u32 arm_step, rsvd;
    u64 s_kbase, s_dmap, s_kmap, s_kblob, s_blob_size;
    u64 s_want[4], s_va[4], s_pa[4];
};
#define DB_ARM_STEPS 10
#define DB_ARM_RETRY (-1000)            /* step could not allocate without waiting: call it again */

int db_arm(void *td, u64 *uap);        /* ring0, via syscall 11 */
int db_hook(u8 *icc_msg);              /* runs from the kernel copy at shutdown */
void db_hook_entry(void);              /* hook_entry.S: pass-through unless db_takeover */
extern u8 db_tramp[32];                /* original 15 bytes + jmp back (filled by db_arm) */
extern volatile u32 db_takeover;       /* set in the kernel copy by db_reboot */
#define K_ICC_DISPLACED  15            /* whole instructions covered by the 14-byte jmp */
int db_reboot(void *td, u64 *uap);
int db_wlanbt(void *td, u64 *uap);     /* ring0: OrbisOS wlanbt(2) = wifi/bt reset (reference hook step) */
int db_install(void *td, u64 *uap);    /* ring0: install the hook with every CPU stopped */
struct db_patch {                      /* lives in the kernel copy; used by db_patch_action */
    u64 site, target;
    volatile u32 arrived, writer, done, rsvd;
};
extern struct db_patch g_patch;
void db_patch_action(void *arg);       /* smp_rendezvous action (runs from the kernel copy) */
void db_find_scanout(struct db_state *s);   /* enabled CRTC with the largest surface */     /* ring0: kern_reboot(RB_AUTOBOOT) -> shutdown path -> hook */

/* small helpers shared by arm.c and hook.c */
static inline u64 db_rdmsr(u32 m)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m));
    return ((u64)hi << 32) | lo;
}
static inline void db_wrmsr(u32 m, u64 v)
{
    __asm__ volatile("wrmsr" :: "c"(m), "a"((u32)v), "d"((u32)(v >> 32)));
}
#define DB_RDCR(n)    ({ u64 v; __asm__ volatile("mov %%cr" #n ", %0" : "=r"(v)); v; })
#define DB_WRCR(n, v) __asm__ volatile("mov %0, %%cr" #n :: "r"((u64)(v)) : "memory")

#endif
