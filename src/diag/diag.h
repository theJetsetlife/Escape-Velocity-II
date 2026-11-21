/* Read-only diagnostic payload — shared definitions (payload <-> tools/diag_recv.py).
 *
 * Wire format (TCP, little-endian):
 *   "DBDIAG01" (8 bytes), then a sequence of records:
 *     struct diag_rec { u32 tag; u32 len; u64 a; u64 b; }  followed by `len` data bytes,
 *     zero-padded to a multiple of 8.
 *
 * Every phase is sent to the PC as soon as it finishes, so if the console dies the last
 * record received shows where it happened.
 */
#ifndef DIAG_H
#define DIAG_H

#include "../ps4.h"

#define DIAG_MAGIC      "DBDIAG01"
#define DIAG_VERSION    3

struct diag_rec {
    u32 tag;
    u32 len;
    u64 a;
    u64 b;
};

enum diag_tag {
    T_HELLO    = 1,  /* a=version b=stage                                        */
    T_STAGE    = 2,  /* a=phase   b=0 enter / 1 leave; data = phase name          */
    T_REGS     = 3,  /* data = struct diag_regs                                   */
    T_MSR      = 4,  /* a=msr b=value                                             */
    T_GATE     = 5,  /* a=gate id b=1 pass / 0 fail; data = name                  */
    T_PCICFG   = 6,  /* a=bus<<16|dev<<8|fn b=method (1 = ECAM); data=256 B  */
    T_MMIO32   = 7,  /* a=phys base b=region id; data = u32 reads                 */
    T_PHYS     = 8,  /* a=phys base b=kind (enum diag_phys_kind); data = bytes    */
    T_ERR      = 9,  /* a=error code (enum diag_err) b=detail                     */
    T_SYSCTL   = 10, /* a=status (0 ok, else -errno) b=value length; data=name\0value */
    T_NOTE     = 11, /* data = text                                               */
    T_RET      = 12, /* a=phase b=kexec return (s64)                              */
    T_END      = 13, /* a=records b=flags (bit0 = buffer was full)                */
    T_PTE      = 14, /* a=VA b=leaf entry; data = u64 level (0 = not present)     */
    T_RUN      = 15, /* DMAP coverage run: a=phys start b=phys end (excl); data=u64 key */
};

enum diag_phys_kind {
    PK_PML4 = 1, PK_PDPT = 2, PK_RSDP = 3, PK_ACPI = 4,
    PK_KTEXT = 5,   /* kernel text read through its DMAP alias (informational) */
    PK_FB    = 6,   /* scanout surface bytes; b = kind | crtc<<16 | (0 start, 1 centre)<<24 */
};

enum diag_region {
    RG_GLUE_BARWIN = 1,   /* 14.4 BAR4 + 0x1c8000..0x1c81ff  glue BAR mask/addr      */
    RG_GLUE_MSI    = 2,   /* 14.4 BAR4 + 0x1c8400..0x1c85ff  MSI control/mask/addr  */
    RG_CHIPID      = 3,   /* 14.4 BAR2 + 0x1100..0x110f                             */
    RG_IOMMU_LO    = 4,   /* IOMMU base + 0x0000..0x007f                            */
    RG_IOMMU_HI    = 5,   /* IOMMU base + 0x2000..0x203f (head/tail/status)         */
    RG_GPU_GMC     = 6,   /* 00:01.0 BAR5: MC_VM_* regs (dword index in data order) */
    RG_GPU_CRTC    = 7,   /* 00:01.0 BAR5: CRTC n (a=base, b=7|n<<8)                */
};

enum diag_err {
    E_FW_GATE   = 1,  /* kernel signature mismatch: not retail 9.00 -> stop          */
    E_DMAP      = 2,  /* DMAP globals implausible or self-check failed               */
    E_NOTMAPPED = 3,  /* b = phys address not present in the direct map             */
    E_NOTUC     = 4,  /* b = MMIO address not uncacheable per MTRR/PAT -> skipped   */
    E_NOECAM    = 5,  /* ECAM disabled/not UC -> PCI-dependent phases skipped                     */
    E_NODEV     = 6,  /* b = bdf expected but absent                                 */
    E_NOBAR     = 7,  /* b = bdf<<8 | bar offset: BAR empty/IO                       */
    E_NORSDP    = 8,
    E_ACPI      = 9,  /* b = phys of bad table                                       */
    E_SOCKET    = 10, /* b = -errno                                                  */
    E_KEXEC     = 11, /* b = return value                                            */
    E_FULL      = 12,
};

struct diag_regs {
    u64 kbase, lstar, dmap;
    u32 pml4pml4i, dmpml4i, dmpdpi, cpu_maxpa;
    u64 cr0, cr3, cr4, efer, rflags;
    u32 cpuid1_eax, cpuid1_ebx, cpuid1_ecx, cpuid1_edx;
    u32 cpuid7_ebx, apic_id;
};

_Static_assert(sizeof(struct diag_rec) == 24, "wire format");
_Static_assert(sizeof(struct diag_regs) == 104, "wire format (tools/diag_recv.py REGS)");

/* Phases run one kexec each; userland flushes to the PC between them. */
enum diag_phase {
    PH_IDENT = 1,   /* firmware gate, kbase, DMAP base + self-check                  */
    PH_CPU   = 2,   /* control regs, MSRs, MTRRs, PAT, AMD memory MSRs              */
    PH_PGTBL = 3,   /* raw PML4 + DMAP PDPT, translation of probe addresses          */
    PH_PCI   = 4,   /* bus 0 config space                                            */
    PH_GLUE  = 5,   /* Aeolia glue windows + MSI block + chip id                     */
    PH_IOMMU = 6,
    PH_GPU   = 7,   /* MC aperture + 6x CRTC primary surface                         */
    PH_ACPI  = 8,   /* RSDP + every table reachable from RSDT/XSDT                   */
    PH_DMAP  = 9,   /* run-length map of every phys range the DMAP maps, 0..16 GiB   */
    PH_LAST  = 9,
};

#define DIAG_CTX_MAGIC  0x3143545847414944ull   /* "DIAGXTC1" */
#define DIAG_BUF_SIZE   (1u << 20)

/* Lives at the start of the mlock'd buffer, so ring0 never touches pageable memory. */
struct diag_ctx {
    u64 magic;
    u8  *out;       /* record area */
    u64 cap, len;
    u64 nrec, flags;
    u32 phase, ok;  /* ok: bit per phase that completed */
    /* state carried between phases (ring0 fills it) */
    u64 kbase, dmap, cr3, ecam;
    u32 ecam_ok, maxpa;
    u64 acpi_root;  /* from sysctl machdep.acpi_root (userland), 0 if unknown */
};

#endif
