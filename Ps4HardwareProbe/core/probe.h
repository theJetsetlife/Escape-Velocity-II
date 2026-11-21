/* probe.h - Ps4HardwareProbe shared definitions (payload <-> logging/probe_recv.py).
 *
 * Wire format (TCP, little-endian): "PS4PRB01", then records
 *   struct prb_rec { u32 tag; u32 len; u64 a; u64 b; } + `len` data bytes, zero-padded to 8.
 * Every phase is flushed to the PC before the next one starts: if the console dies, the last
 * T_PHASE enter names the phase.
 */
#ifndef PROBE_H
#define PROBE_H

#include "types.h"                   /* u8..u64: PS4 Payload SDK libPS4/include/types.h */

#define PRB_MAGIC      "PS4PRB01"
#define PRB_VERSION    1
#define PRB_CTX_MAGIC  0x5854434245425250ull   /* "PRBEBCTX" */
#define PRB_BUF_SIZE   (4u << 20)
#define PRB_SCRATCH    0x10000u
#define PRB_MAXDEV     96

struct prb_rec { u32 tag, len; u64 a, b; };

enum prb_tag {
    T_HELLO = 1,   /* a=version b=env                                     */
    T_PHASE = 2,   /* a=phase b=0 enter/1 leave; data=name                 */
    T_DIAG  = 3,   /* a=diag id b=status (enum prb_status); data=reason    */
    T_MSR   = 4,   /* a=msr b=value                                       */
    T_CPUID = 5,   /* a=leaf | sub<<32; data=eax,ebx,ecx,edx               */
    T_CREG  = 6,   /* a=0/3/4 CRn, 0x80 EFER, 0x81 RFLAGS; b=value         */
    T_PCICFG= 7,   /* a=bus<<16|dev<<8|fn; data=256 B config space          */
    T_MMIO32= 8,   /* a=phys b=region id; data=u32 reads                   */
    T_PHYS  = 9,   /* a=phys b=kind; data=bytes                            */
    T_SYSCTL= 10,  /* a=status b=len; data=name\0value                     */
    T_NOTE  = 11,  /* data=text                                            */
    T_RET   = 12,  /* a=phase b=kexec return                               */
    T_END   = 13,  /* a=records b=flags|ok<<8                              */
    T_RUN   = 15,  /* DMAP run: a=start b=end; data=u64 key                */
    T_IDENT = 16,  /* data=struct prb_ident                                */
    T_FW    = 17,  /* a=source (enum prb_fwsrc) b=version (900=9.00, 0=?)   */
    T_TSC   = 18,  /* a=sample b=tsc                                       */
    T_ACPI  = 19,  /* a=phys b=checksum ok; data=sig[4] + u32 len          */
    T_CLOCK = 20,  /* a=sample b=CLOCK_MONOTONIC ns (userland)             */
};

enum prb_status { ST_OK = 0, ST_UNSUPPORTED = 1, ST_SKIPPED_SAFETY = 2, ST_FAILED = 3 };

enum prb_diag {
    D_ENV = 1, D_SMEP = 2, D_KEXEC = 6, D_FW_USER = 3, D_FW_KERNEL = 4, D_DMAP_PROOF = 5,
    D_CPU_ID = 10, D_CPU_CREGS = 11, D_CPU_MSR = 12, D_CPU_TOPOLOGY = 13, D_TSC = 14,
    D_MEM_SYSCTL = 20, D_MEM_MTRR = 21, D_MEM_DMAP = 22,
    D_ACPI = 30,
    D_PCI_ECAM = 40, D_PCI_SCAN = 41, D_PLATFORM = 42, D_SB_IDENT = 43, D_SB_REGS = 44,
    D_IRQ_LAPIC = 50, D_IRQ_MSI = 51, D_IRQ_IOAPIC = 52,
    D_GPU_ID = 60, D_GPU_FB = 61,
    D_USB = 70, D_SATA = 80, D_SDHCI = 81, D_NET = 90,
    D_HPET = 100,
};

enum prb_phase { PH_FW = 1, PH_CPU, PH_TSC, PH_PCI, PH_SB, PH_IRQ, PH_GPU, PH_ACPI, PH_DEV, PH_MEM,
                 PH_LAST = PH_MEM };

enum prb_env    { ENV_UNKNOWN = 0, ENV_WEBKITTY = 1, ENV_GOLDHEN = 2, ENV_COUNT };
/* read level: what MMIO a run may touch besides PCI config space */
enum prb_level  { LVL_CONFIG = 0,      /* PCI config + CPU registers only                      */
                  LVL_HWPROVEN = 1,    /* + reads made on real PS4 hardware: by this project's runs, or
                                          unconditionally at every boot by the PS4 Linux driver of
                                          that chip (chip id, RTC). The default.                 */
                  LVL_DOCUMENTED = 2 };/* + windows a public driver uses, not proven as plain reads */
enum prb_fwstate{ FWS_UNKNOWN = 0,     /* version not known: no kernel offsets used                  */
                  FWS_UNSUPPORTED = 1, /* no profile, or a profile the live kernel rejects           */
                  FWS_IDENTIFIED = 2,  /* kernel identity confirmed (OpenGoldHEN), no DMAP profile   */
                  FWS_VALIDATED = 3,   /* DMAP profile matches the live kernel, not hardware-verified */
                  FWS_VERIFIED = 4 };  /* DMAP profile hardware-verified                             */
enum prb_fwsrc  { FWSRC_LIBC = 1, FWSRC_HINT = 2, FWSRC_KERNEL = 3, FWSRC_SYSCTL = 4, FWSRC_IDENT = 5 };
enum prb_sbf    { SBF_NONE = 0, SBF_AEOLIA, SBF_BELIZE, SBF_BAIKAL, SBF_UNKNOWN_SONY };
enum prb_board  { BRD_UNKNOWN = 0, BRD_FAT_11, BRD_FAT_12, BRD_SLIM, BRD_PRO };
/* verification state of a profile (docs/COMPATIBILITY.md) */
enum prb_vstate { VS_UNTESTED = 0, VS_EXPERIMENTAL = 1, VS_PARTIAL = 2, VS_VERIFIED = 3 };

/* config block at blob offset 0x10, patched by probe_recv.py (network-order ip/port) */
struct prb_cfg {
    char magic[8];                   /* "PRBCFG01" */
    u8 ip[4], port[2];
    u8 env, level, strict, rsvd;     /* strict: only hardware-verified firmware offsets */
    u16 fw_hint, rsvd2;              /* operator-declared version (900), 0 = none       */
};

struct prb_pcidev { u8 bus, dev, fn, hdr; u16 vendor, device; u32 class_rev; };

struct prb_ident {
    u16 gpu_device, sb_glue_device;
    u8  gpu_found, sb_family, sb_funcs, board;
    u8  board_state, sb_state, fw_state, env;
    u8  ring0, level, strict, chipid_ok;
    u16 fw_kernel, fw_user;
    u32 cpuid1_eax, sb_chipid[3];
    u8  glue_bus, glue_dev, glue_fn, profile_state;   /* selected profile: min of board/sb/fw */
};

struct prb_ctx {
    u64 magic;
    u32 phase, ok;
    u8 *out;
    u32 len, cap, nrec, flags;       /* flags bit0 = buffer full */
    struct prb_cfg cfg;
    u64 kbase, dmap, cr3;
    u32 maxpa, fam16h;
    u64 ecam;
    u32 ecam_ok, ecam_buses;
    u64 acpi_root, hpet;
    const void *fwrow;               /* struct db_kofs * of the accepted row */
    u16 fw_user, rsvd;
    struct prb_ident id;
    u32 ndev, rsvd2;
    struct prb_pcidev dev[PRB_MAXDEV];
};

#endif
