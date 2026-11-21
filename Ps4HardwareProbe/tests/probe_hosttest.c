/* probe_hosttest.c - every backend and safety path against fake hardware (tests/mock_hal.c).
 *   probe_hosttest OUTDIR [kernel_900_dump.bin]
 * Each scenario runs the real ring0 phases in order (as core/main.c does), writes the record stream
 * to OUTDIR/<scenario>.bin for logging/test_decode.py, and checks the final status of each listed
 * diagnostic plus the identification result. Mock hardware != a PS4: these prove the logic, the
 * gates and the backend selection, not real hardware behaviour. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock.h"
#include "../core/probe.h"
#include "../logging/rec.h"
#include "../firmware/fw.h"
#include "../adapters/adapters.h"

int prb_ring0(void *td, u64 *uap);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (c) printf("    ok   "); else { printf("    FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

struct expect { u32 diag, status; };
struct scenario {
    const char *name;
    struct mock_cfg m;
    u16 fw_user, fw_hint; u8 level, strict;
    u8 board, sb, fw_state, chipid, profile;
    int unknown_fw_bound;            /* check: no kernel byte read beyond the Xfast signature */
    struct expect e[16];
};

#define HOSTB  { 0x00, 0, 0x1022, 0x1436, 0x06000000, { 0 } }
#define GPU(id){ 0x01, 0, 0x1002, id, 0x03000000, { 0, 0, 0, 0, 0, 0xe4800000 } }
#define AEOLIA(base) \
    { 0x14, 0, 0x104d, base + 0, 0x08800000, { 0 } }, { 0x14, 1, 0x104d, base + 1, 0x02000000, { 0 } }, \
    { 0x14, 2, 0x104d, base + 2, 0x01060100, { 0 } }, { 0x14, 3, 0x104d, base + 3, 0x08050100, { 0 } }, \
    { 0x14, 4, 0x104d, base + 4, 0x08800000, { 0, 0, 0xd0100000, 0, 0xc8000000, 0 } }, \
    { 0x14, 5, 0x104d, base + 5, 0x08800000, { 0 } }, { 0x14, 6, 0x104d, base + 6, 0x05000000, { 0 } }, \
    { 0x14, 7, 0x104d, base + 7, 0x0c033000, { 0 } }
#define AEOLIA_IDS(a,g,s,d,p,m,x,u) \
    { 0x14, 0, 0x104d, a, 0x08800000, { 0 } }, { 0x14, 1, 0x104d, g, 0x02000000, { 0 } }, \
    { 0x14, 2, 0x104d, s, 0x01060100, { 0 } }, { 0x14, 3, 0x104d, d, 0x08050100, { 0 } }, \
    { 0x14, 4, 0x104d, p, 0x08800000, { 0, 0, 0xd0100000, 0, 0xc8000000, 0 } }, \
    { 0x14, 5, 0x104d, m, 0x08800000, { 0 } }, { 0x14, 6, 0x104d, x, 0x05000000, { 0 } }, \
    { 0x14, 7, 0x104d, u, 0x0c033000, { 0 } }
#define FORBID_ICC  .forbid = { { 0xc8184000, 0x1000 }, { 0xd0107800, 0x1000 } }, .nforbid = 2

static struct scenario S[] = {
    { "fat11-aeolia-900", { .fam16h = 1, .kernel_ver = 900, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9920), AEOLIA_IDS(0x908f,0x909e,0x909f,0x90a0,0x90a1,0x90a2,0x90a3,0x90a4) }, .ndevs = 10, FORBID_ICC },
      900, 0, LVL_HWPROVEN, 1, BRD_FAT_11, SBF_AEOLIA, FWS_VERIFIED, 1, VS_PARTIAL, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_OK }, { D_PCI_SCAN, ST_OK }, { D_PLATFORM, ST_OK },
        { D_SB_REGS, ST_OK }, { D_GPU_FB, ST_OK }, { D_IRQ_LAPIC, ST_SKIPPED_SAFETY }, { D_ACPI, ST_OK },
        { D_USB, ST_OK }, { D_SATA, ST_OK }, { D_SDHCI, ST_OK }, { D_NET, ST_OK }, { D_MEM_DMAP, ST_OK },
        { D_HPET, ST_UNSUPPORTED }, { 0, 0 } } },
    { "fat12-belize-900-level1", { .fam16h = 1, .kernel_ver = 900, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9922), AEOLIA_IDS(0x90c8,0x90c9,0x90ca,0x90cb,0x90cc,0x90cd,0x90ce,0x90cf) }, .ndevs = 10, FORBID_ICC },
      900, 0, LVL_HWPROVEN, 1, BRD_FAT_12, SBF_BELIZE, FWS_VERIFIED, 1, VS_UNTESTED, 0,
      { { D_PLATFORM, ST_OK }, { D_SB_REGS, ST_OK }, { D_GPU_FB, ST_SKIPPED_SAFETY }, { 0, 0 } } },
    { "fat12-belize-900-level2", { .fam16h = 1, .kernel_ver = 900, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9922), AEOLIA_IDS(0x90c8,0x90c9,0x90ca,0x90cb,0x90cc,0x90cd,0x90ce,0x90cf) }, .ndevs = 10, FORBID_ICC },
      900, 0, LVL_DOCUMENTED, 1, BRD_FAT_12, SBF_BELIZE, FWS_VERIFIED, 1, VS_UNTESTED, 0,
      { { D_SB_REGS, ST_OK }, { D_GPU_FB, ST_OK }, { D_IRQ_LAPIC, ST_OK }, { 0, 0 } } },
    { "slim-baikal-1100-validated", { .fam16h = 1, .kernel_ver = 1100, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9923),
                  { 0x14, 0, 0x104d, 0x90d7, 0x08800000, { 0 } }, { 0x14, 2, 0x104d, 0x90d9, 0x01060100, { 0 } },
                  { 0x14, 4, 0x104d, 0x90db, 0x08800000, { 0, 0, 0xd0000000, 0, 0xc9000000, 0 } },
                  { 0x14, 7, 0x104d, 0x90de, 0x0c033000, { 0 } } }, .ndevs = 6, FORBID_ICC },
      1100, 0, LVL_DOCUMENTED, 0, BRD_SLIM, SBF_BAIKAL, FWS_VALIDATED, 1, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_OK }, { D_PLATFORM, ST_OK }, { D_SB_REGS, ST_OK }, { 0, 0 } } },
    { "slim-baikal-1100-strict", { .fam16h = 1, .kernel_ver = 1100, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9923), { 0x14, 4, 0x104d, 0x90db, 0x08800000, { 0, 0, 0xd0000000, 0, 0xc9000000, 0 } } },
        .ndevs = 3, FORBID_ICC },
      1100, 0, LVL_HWPROVEN, 1, BRD_UNKNOWN, SBF_NONE, FWS_VALIDATED, 0, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_SKIPPED_SAFETY }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { D_CPU_ID, ST_OK }, { D_CPU_MSR, ST_OK },
        { D_PCI_ECAM, ST_UNSUPPORTED }, { D_PCI_SCAN, ST_UNSUPPORTED }, { D_PLATFORM, ST_UNSUPPORTED },
        { D_ACPI, ST_UNSUPPORTED }, { D_MEM_DMAP, ST_UNSUPPORTED }, { 0, 0 } } },
    { "pro-baikal-unknown-fw", { .fam16h = 1, .kernel_ver = 0, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9924), { 0x14, 4, 0x104d, 0x90db, 0x08800000, { 0, 0, 0xd0000000, 0, 0xc9000000, 0 } } },
        .ndevs = 3, FORBID_ICC },
      0, 0, LVL_DOCUMENTED, 1, BRD_UNKNOWN, SBF_NONE, FWS_UNKNOWN, 0, VS_UNTESTED, 1,
      { { D_FW_KERNEL, ST_UNSUPPORTED }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { D_CPU_ID, ST_OK }, { D_PCI_SCAN, ST_UNSUPPORTED }, { 0, 0 } } },
    { "unknown-version-1600", { .fam16h = 1, .kernel_ver = 0, .devs = { HOSTB }, .ndevs = 1 },
      1600, 0, LVL_HWPROVEN, 1, BRD_UNKNOWN, SBF_NONE, FWS_UNSUPPORTED, 0, VS_UNTESTED, 1,
      { { D_FW_KERNEL, ST_UNSUPPORTED }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { 0, 0 } } },
    { "unknown-hardware", { .fam16h = 1, .kernel_ver = 900, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x1234), { 0x14, 0, 0x104d, 0x9998, 0x08800000, { 0 } }, { 0x14, 4, 0x104d, 0x9999, 0x08800000, { 0 } },
                  { 0x10, 0, 0x8086, 0x1e31, 0x0c033000, { 0xc6000000 } } }, .ndevs = 5, FORBID_ICC },
      900, 0, LVL_DOCUMENTED, 1, BRD_UNKNOWN, SBF_UNKNOWN_SONY, FWS_VERIFIED, 0, VS_UNTESTED, 0,
      { { D_PLATFORM, ST_UNSUPPORTED }, { D_SB_IDENT, ST_UNSUPPORTED }, { D_SB_REGS, ST_SKIPPED_SAFETY },
        { D_GPU_ID, ST_OK }, { D_USB, ST_OK }, { 0, 0 } } },
    { "mixed-southbridge", { .fam16h = 1, .kernel_ver = 900,
        .devs = { HOSTB, GPU(0x9920), { 0x14, 0, 0x104d, 0x908f, 0x08800000, { 0 } },
                  { 0x14, 4, 0x104d, 0x90a1, 0x08800000, { 0, 0, 0xd0100000, 0, 0xc8000000, 0 } },
                  { 0x14, 2, 0x104d, 0x90d9, 0x01060100, { 0 } } }, .ndevs = 5, FORBID_ICC },
      900, 0, LVL_DOCUMENTED, 1, BRD_FAT_11, SBF_UNKNOWN_SONY, FWS_VERIFIED, 0, VS_UNTESTED, 0,
      { { D_PLATFORM, ST_UNSUPPORTED }, { D_SB_REGS, ST_SKIPPED_SAFETY }, { 0, 0 } } },
    { "not-family-16h", { .fam16h = 0, .kernel_ver = 900, .devs = { HOSTB }, .ndevs = 1 },
      900, 0, LVL_DOCUMENTED, 1, BRD_UNKNOWN, SBF_NONE, FWS_VERIFIED, 0, VS_UNTESTED, 0,
      { { D_CPU_ID, ST_OK }, { D_CPU_MSR, ST_SKIPPED_SAFETY }, { D_MEM_MTRR, ST_SKIPPED_SAFETY },
        { D_PCI_ECAM, ST_SKIPPED_SAFETY }, { D_IRQ_LAPIC, ST_UNSUPPORTED }, { 0, 0 } } },
    { "declared-version-mismatch", { .fam16h = 1, .kernel_ver = 900, .devs = { HOSTB }, .ndevs = 1 },
      900, 1100, LVL_HWPROVEN, 1, BRD_UNKNOWN, SBF_NONE, FWS_UNSUPPORTED, 0, VS_UNTESTED, 1,
      { { D_FW_KERNEL, ST_FAILED }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { 0, 0 } } },
    { "profile-rejected-by-kernel", { .fam16h = 1, .kernel_ver = 505, .devs = { HOSTB }, .ndevs = 1 },
      900, 0, LVL_HWPROVEN, 1, BRD_UNKNOWN, SBF_NONE, FWS_UNSUPPORTED, 0, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_FAILED }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { D_PCI_SCAN, ST_UNSUPPORTED }, { 0, 0 } } },
    { "pro-baikal-1352-default", { .fam16h = 1, .kernel_ver = 1352, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9924),
                  { 0x14, 0, 0x104d, 0x90d7, 0x08800000, { 0 } }, { 0x14, 2, 0x104d, 0x90d9, 0x01060100, { 0 } },
                  { 0x14, 3, 0x104d, 0x90da, 0x08050100, { 0 } }, { 0x14, 1, 0x104d, 0x90d8, 0x02000000, { 0 } },
                  { 0x14, 4, 0x104d, 0x90db, 0x08800000, { 0, 0, 0xd0000000, 0, 0xc9000000, 0 } },
                  { 0x14, 7, 0x104d, 0x90de, 0x0c033000, { 0 } } }, .ndevs = 8, FORBID_ICC },
      1352, 0, LVL_HWPROVEN, 0, BRD_PRO, SBF_BAIKAL, FWS_VALIDATED, 1, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_OK }, { D_PLATFORM, ST_OK }, { D_SB_REGS, ST_OK },
        { D_GPU_FB, ST_SKIPPED_SAFETY }, { D_USB, ST_OK }, { D_SATA, ST_OK }, { D_SDHCI, ST_OK }, { D_NET, ST_OK },
        { D_ACPI, ST_OK }, { 0, 0 } } },
    { "fat11-aeolia-904-point-release", { .fam16h = 1, .kernel_ver = 903, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9920), AEOLIA_IDS(0x908f,0x909e,0x909f,0x90a0,0x90a1,0x90a2,0x90a3,0x90a4) }, .ndevs = 10, FORBID_ICC },
      904, 0, LVL_HWPROVEN, 0, BRD_FAT_11, SBF_AEOLIA, FWS_VALIDATED, 1, VS_EXPERIMENTAL, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_OK }, { D_PLATFORM, ST_OK }, { D_GPU_FB, ST_OK }, { 0, 0 } } },
    { "fat11-aeolia-702-identity-only", { .fam16h = 1, .kernel_ver = 702, .devs = { HOSTB }, .ndevs = 1 },
      702, 0, LVL_HWPROVEN, 0, BRD_UNKNOWN, SBF_NONE, FWS_IDENTIFIED, 0, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { D_CPU_MSR, ST_OK }, { D_PCI_SCAN, ST_UNSUPPORTED }, { 0, 0 } } },
    { "pro-baikal-1400-two-sources", { .fam16h = 1, .kernel_ver = 1400, .lapic_base = 0xfee00000,
        .devs = { HOSTB, GPU(0x9924), { 0x14, 0, 0x104d, 0x90d7, 0x08800000, { 0 } },
                  { 0x14, 4, 0x104d, 0x90db, 0x08800000, { 0, 0, 0xd0000000, 0, 0xc9000000, 0 } } }, .ndevs = 4, FORBID_ICC },
      1400, 0, LVL_HWPROVEN, 0, BRD_PRO, SBF_BAIKAL, FWS_VALIDATED, 1, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_OK }, { D_PLATFORM, ST_OK }, { D_SB_REGS, ST_OK }, { 0, 0 } } },
    { "two-sources-disagree", { .fam16h = 1, .kernel_ver = 900, .break_ident = 1, .devs = { HOSTB }, .ndevs = 1 },
      900, 0, LVL_HWPROVEN, 0, BRD_UNKNOWN, SBF_NONE, FWS_UNSUPPORTED, 0, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_FAILED }, { D_DMAP_PROOF, ST_UNSUPPORTED }, { 0, 0 } } },
    { "dmap-proof-fails", { .fam16h = 1, .kernel_ver = 900, .break_dmap_proof = 1, .devs = { HOSTB }, .ndevs = 1 },
      900, 0, LVL_HWPROVEN, 1, BRD_UNKNOWN, SBF_NONE, FWS_VERIFIED, 0, VS_UNTESTED, 0,
      { { D_FW_KERNEL, ST_OK }, { D_DMAP_PROOF, ST_FAILED }, { D_PCI_SCAN, ST_UNSUPPORTED }, { D_ACPI, ST_UNSUPPORTED }, { 0, 0 } } },
};

/* last T_DIAG status for `id` in the stream, 0xff = absent */
static u32 status_of(const u8 *buf, u32 len, u32 id)
{
    u32 st = 0xff;
    for (u32 o = 0; o + sizeof(struct prb_rec) <= len;) {
        const struct prb_rec *r = (const struct prb_rec *)(buf + o);
        if (r->tag == T_DIAG && r->a == id) st = (u32)r->b;
        o += sizeof *r + ((r->len + 7) & ~7u);
    }
    return st;
}

static void self_parse_tests(void)
{
    printf("fw_from_self (libc.sprx SCE header parse):\n");
    u8 b[512]; memset(b, 0, sizeof b);
    b[0] = 0x4f; b[1] = 0x15; b[2] = 0x3d; b[3] = 0x1d; b[24] = 2;            /* 2 segments */
    u32 elf = 32 + 2 * 32;
    b[elf] = 0x7f; b[elf + 1] = 'E'; b[elf + 52] = 64; b[elf + 54] = 56; b[elf + 56] = 3;
    u32 sce = (elf + 64 + 3 * 56 + 15) & ~15u;
    u64 fw = 0x0000090000000000ull; memcpy(b + sce + 24, &fw, 8);
    CHECK(fw_from_self(b, sizeof b) == 900, "09.00 -> 900");
    fw = 0x0000135200000000ull; memcpy(b + sce + 24, &fw, 8);
    CHECK(fw_from_self(b, sizeof b) == 1352, "13.52 -> 1352");
    fw = 0x00000a0000000000ull; memcpy(b + sce + 24, &fw, 8);
    CHECK(fw_from_self(b, sizeof b) == 0, "non-BCD version refused");
    b[0] = 0;
    CHECK(fw_from_self(b, sizeof b) == 0, "bad SELF magic refused");
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : "hosttest";
    const char *kdump = argc > 2 && argv[2][0] ? argv[2] : 0;
    char path[512];
    snprintf(path, sizeof path, "mkdir -p '%s'", outdir);
    if (system(path)) return 1;
    self_parse_tests();
    printf("loader kexec contracts (WebKitty psfree-lapse 11, relapse 661, GoldHEN 11):\n");
    const struct prb_adapter *wk = adapter_for(ENV_WEBKITTY), *gh = adapter_for(ENV_GOLDHEN);
    CHECK(adapter_kexec_nr(wk, 900) == 11 && adapter_kexec_nr(wk, 755) == 11, "WebKitty 9.00 / 7.55 -> syscall 11");
    CHECK(adapter_kexec_nr(wk, 1100) == 661 && adapter_kexec_nr(wk, 1352) == 661, "WebKitty 11.00 / 13.52 -> syscall 661");
    CHECK(adapter_kexec_nr(wk, 1050) == 0 && adapter_kexec_nr(wk, 0) == 0, "WebKitty 10.50 / unknown -> no ring0");
    CHECK(adapter_kexec_nr(gh, 1300) == 11 && adapter_kexec_nr(gh, 0) == 11, "GoldHEN any version -> syscall 11");
    CHECK(adapter_for(ENV_UNKNOWN) == 0, "undeclared loader -> no adapter");
    u8 *buf = aligned_alloc(4096, PRB_BUF_SIZE);
    for (u32 si = 0; si < sizeof S / sizeof S[0]; si++) {
        struct scenario *s = &S[si];
        printf("%s%s:\n", s->name, (kdump && s->m.kernel_ver == 900) ? " (real 9.00 kernel dump)" : "");
        mock_reset(&s->m, kdump);
        memset(buf, 0, PRB_BUF_SIZE);
        struct prb_ctx *c = (struct prb_ctx *)buf;
        c->magic = PRB_CTX_MAGIC; c->out = buf + 0x10000; c->cap = PRB_BUF_SIZE - 0x20000;
        memcpy(c->cfg.magic, "PRBCFG01", 8);
        c->cfg.env = ENV_GOLDHEN; c->cfg.level = s->level; c->cfg.strict = s->strict; c->cfg.fw_hint = s->fw_hint;
        c->fw_user = s->fw_user; c->id.fw_user = s->fw_user;
        c->id.env = ENV_GOLDHEN; c->id.level = s->level; c->id.strict = s->strict;
        rec(c, T_HELLO, PRB_VERSION, ENV_GOLDHEN, 0, 0);
        rec(c, T_FW, FWSRC_LIBC, s->fw_user, 0, 0);
        for (u32 ph = PH_FW; ph <= PH_LAST; ph++) {
            c->phase = ph;
            rec_s(c, T_PHASE, ph, 0, "phase");
            u64 uap[2] = { 0, (u64)c };
            prb_ring0(0, uap);
            rec_s(c, T_PHASE, ph, 1, "phase");
        }
        rec(c, T_IDENT, 0, 0, &c->id, sizeof c->id);
        rec(c, T_END, c->nrec, c->flags | (u64)c->ok << 8, 0, 0);
        for (u32 k = 0; s->e[k].diag; k++) {
            u32 st = status_of(c->out, c->len, s->e[k].diag);
            CHECK(st == s->e[k].status, "diag %u status %u (want %u)", s->e[k].diag, st, s->e[k].status);
        }
        CHECK(c->id.board == s->board, "board %u (want %u)", c->id.board, s->board);
        CHECK(c->id.sb_family == s->sb, "southbridge %u (want %u)", c->id.sb_family, s->sb);
        CHECK(c->id.fw_state == s->fw_state, "firmware state %u (want %u)", c->id.fw_state, s->fw_state);
        CHECK(c->id.chipid_ok == s->chipid, "chip id read %u (want %u)", c->id.chipid_ok, s->chipid);
        CHECK(c->id.profile_state == s->profile, "selected profile state %u (want %u)", c->id.profile_state, s->profile);
        if (s->unknown_fw_bound)
            CHECK(mock_max_kern_off() < FW_K_XFAST + 8, "no kernel byte read past the Xfast signature (max +0x%llx)",
                  (unsigned long long)mock_max_kern_off());
        CHECK(!(c->flags & 1), "record buffer did not overflow (%u records, %u bytes)", c->nrec, c->len);
        snprintf(path, sizeof path, "%s/%s.bin", outdir, s->name);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(PRB_MAGIC, 1, 8, f); fwrite(c->out, 1, c->len, f); fclose(f); }
    }
    printf("RESULT: %s (%d checks, %d failed)\n", fails ? "FAIL" : "all checks passed", checks, fails);
    return fails ? 1 : 0;
}
