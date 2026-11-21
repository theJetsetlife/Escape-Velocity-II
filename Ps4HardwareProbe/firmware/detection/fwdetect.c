/* fwdetect.c - which system software is running.
 *   userland: fw_from_self() over libc.sprx (independent of the kernel image)
 *   ring0   : fw_kernel_identify(): LSTAR -> kbase, Xfast_syscall signature, then ONLY the table row
 *             of the userland/declared version is checked against the live kernel (db_fw_check:
 *             gadget, 8 function prologues, 3 patch-site shapes, ICC prologue, DMAP globals,
 *             kernel_map). An unknown version reads nothing beyond the 8 signature bytes. */
#include "../fw.h"
#include "../../core/hal.h"
#include "../../logging/rec.h"
#include "fwsel.h"
#include "fwident.h"

static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static u64 rd64(const u8 *p) { u64 v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
static u32 bcd(u32 x) { return (x >> 4) * 10 + (x & 15); }

u16 fw_from_self(const u8 *b, u32 n)
{
    if (n < 32 || !(b[0] == 0x4f && b[1] == 0x15 && b[2] == 0x3d && b[3] == 0x1d)) return 0;   /* SELF magic */
    u32 elf = 32 + (u32)rd16(b + 24) * 32;                    /* SelfHeader(32) + entries(32 each) */
    if (elf + 64 > n || !(b[elf] == 0x7f && b[elf + 1] == 'E')) return 0;
    u32 sce = elf + rd16(b + elf + 52) + (u32)rd16(b + elf + 56) * rd16(b + elf + 54);   /* ehsize + phnum*phentsize */
    sce = (sce + 15) & ~15u;
    if (sce + 32 > n) return 0;
    u64 fw = rd64(b + sce + 24);
    u32 hi = (u32)(fw >> 40) & 0xff, lo = (u32)(fw >> 32) & 0xff;
    if ((hi & 0xf) > 9 || (hi >> 4) > 9 || (lo & 0xf) > 9 || (lo >> 4) > 9) return 0;
    u32 v = bcd(hi) * 100 + bcd(lo);
    return (v >= 100 && v < 2000) ? (u16)v : 0;
}

/* OpenGoldHEN identity profile: text sites only first, the kernel_map data value only after they pass */
static const struct fw_ident *ident_for(u16 ver)
{
    for (u32 i = 0; i < FW_IDENT_COUNT; i++) if (fw_ident_table[i].ver == ver) return &fw_ident_table[i];
    return 0;
}

static u32 ident_check(const u8 *kb, const struct fw_ident *r, u32 *rop_ok)
{
    *rop_ok = r->kexec_rop && kb[r->kexec_rop] == 0xff && kb[r->kexec_rop + 1] == 0x26;
    for (u32 i = 0; i < 8; i++) {
        const u8 *p = kb + r->site[i];
        if (r->site[i] && !(p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89 && p[3] == 0xe5)) return i + 1;
    }
    if ((*(const u64 *)(kb + r->kernel_map) >> 47) != 0x1ffff) return 9;
    return 0;
}

u32 fw_kernel_identify(struct prb_ctx *c)
{
    static const u8 sig_xfast[8] = { 0x0f, 0x01, 0xf8, 0x65, 0x48, 0x89, 0x24, 0x25 };   /* swapgs; mov %rsp,%gs: */
    u64 lstar = hal_rdmsr(0xc0000082);
    rec(c, T_MSR, 0xc0000082, lstar, 0, 0);
    u64 kbase = lstar - FW_K_XFAST;              /* 0x1c0 on all 32 OpenGoldHEN versions (mkfwident.py checks) */
    if ((kbase & 0x3fff) || (kbase >> 32) != 0xffffffffull) {
        diag(c, D_FW_KERNEL, ST_UNSUPPORTED, "LSTAR not inside an OrbisOS-shaped kernel half");
        return 0;
    }
    const u8 *kb = hal_kern(kbase);
    if (!prb_memeq(kb + FW_K_XFAST, sig_xfast, sizeof sig_xfast)) {
        diag(c, D_FW_KERNEL, ST_UNSUPPORTED, "Xfast_syscall signature not at LSTAR");
        return 0;
    }
    c->kbase = kbase;
    u16 ver = c->fw_user ? c->fw_user : c->cfg.fw_hint;
    if (c->fw_user && c->cfg.fw_hint && c->cfg.fw_hint != c->fw_user) {
        c->id.fw_state = FWS_UNSUPPORTED;
        diag(c, D_FW_KERNEL, ST_FAILED, "declared version disagrees with libc.sprx: no offsets used");
        return 0;
    }
    if (!ver) {
        c->id.fw_state = FWS_UNKNOWN;
        diag(c, D_FW_KERNEL, ST_UNSUPPORTED, "version unknown (libc.sprx unreadable, none declared): no offsets used");
        return 0;
    }
    const struct fw_ident *idp = ident_for(ver);
    const struct db_kofs *row = fw_row_for(ver);
    if (!idp && !row) {
        c->id.fw_state = FWS_UNSUPPORTED;
        diag(c, D_FW_KERNEL, ST_UNSUPPORTED, "no identity or offset profile for this version");
        return 0;
    }
    /* source 1: OpenGoldHEN identity (independent of the offset table) */
    if (idp) {
        u32 rop_ok, bad = ident_check(kb, idp, &rop_ok);
        rec(c, T_FW, FWSRC_IDENT, bad ? 0 : ver, &bad, 4);
        rec_s(c, T_NOTE, rop_ok, idp->kexec_rop, rop_ok ? "GoldHEN kexec_rop is jmp [rsi]" : "GoldHEN kexec_rop is not jmp [rsi] on this kernel");
        if (bad) {
            c->id.fw_state = FWS_UNSUPPORTED;
            diag(c, D_FW_KERNEL, ST_FAILED, "kernel does not match the OpenGoldHEN identity profile: no offsets used");
            return 0;
        }
        c->id.fw_kernel = ver;
        if (!row) {
            c->id.fw_state = FWS_IDENTIFIED;
            diag(c, D_FW_KERNEL, ST_OK, "identity confirmed (OpenGoldHEN); no DMAP profile published for this version");
            return 0;
        }
    }
    /* source 2: the DMAP offset row, checked site by site */
    u32 why = db_fw_check(kb, FW_KMAX, row, 1);
    if (why != FW_OK) {
        c->id.fw_state = FWS_UNSUPPORTED;
        rec(c, T_FW, FWSRC_KERNEL, 0, &why, 4);
        diag(c, D_FW_KERNEL, ST_FAILED, idp ? "identity confirmed but the DMAP profile is rejected by the live kernel"
                                            : "profile rejected by the live kernel (see T_FW reason)");
        return 0;
    }
    c->fwrow = row;
    c->id.fw_kernel = ver;
    u16 base = fw_row_base(ver);
    rec(c, T_FW, FWSRC_KERNEL, ver, &base, 2);                     /* data = row used */
    if (fw_row_verified(row) && base == ver) {
        c->id.fw_state = FWS_VERIFIED;
        diag(c, D_FW_KERNEL, ST_OK, idp ? "kernel matches a hardware-verified profile and the OpenGoldHEN identity"
                                        : "kernel matches a hardware-verified profile");
        return 1;
    }
    c->id.fw_state = FWS_VALIDATED;
    if (c->cfg.strict) {
        diag(c, D_FW_KERNEL, ST_SKIPPED_SAFETY,
             "kernel matches a community profile (not hardware-verified); strict mode: offsets unused");
        c->fwrow = 0;
        return 0;
    }
    diag(c, D_FW_KERNEL, ST_OK, idp ? "two independent sources (offset table + OpenGoldHEN) match the live kernel"
                                    : "kernel matches a community profile, validated at runtime only");
    return 1;
}
