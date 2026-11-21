/* main.c - userland half (PS4 Payload SDK, GoldHEN payload convention: crt0 `jmp _main`,
 * int _main(struct thread *td), libPS4 resolves libkernel/libc/libnet at runtime).
 *   connect -> HELLO -> environment, sysctls, firmware (SDK get_firmware: libc.sprx SCE header)
 *   -> SMEP/SMAP gate -> per phase: PHASE enter, flush, kexec(prb_ring0), RET, flush -> IDENT, END.
 * No reporting channel -> nothing privileged runs. */
#include "ps4.h"                                     /* libPS4 umbrella header */
#include "probe.h"
#include "../logging/rec.h"
#include "../adapters/adapters.h"

extern struct prb_cfg prb_cfg;                        /* core/crt0.s, patchable at blob +0x10 */
int prb_ring0(void *td, u64 *uap);

static int g_fd = -1;
static struct prb_ctx *C;
static u8 *g_scratch;

/* kexec through the loader's sysent entry: syscall `nr` (11 or 661, adapters/), fn(td, uap) in ring0 */
static long kexec_nr(long nr, void *fn, void *arg)
{
    long rax = nr;
    __asm__ volatile("syscall" : "+a"(rax), "+D"(fn), "+S"(arg) :: "rcx", "rdx", "r8", "r9", "r10", "r11", "memory", "cc");
    return rax;
}

static int send_all(const void *p, u64 n)
{
    const u8 *b = p;
    while (n) {
        int w = sceNetSend(g_fd, b, n, 0);
        if (w <= 0) return -1;
        b += w; n -= (u64)w;
    }
    return 0;
}

static int flush(u32 linger_ms)
{
    int r = send_all(C->out, C->len);
    C->len = 0;
    if (linger_ms) sceKernelUsleep(linger_ms * 1000);
    return r;
}

static int connect_pc(void)
{
    struct sockaddr_in sa;
    prb_zero(&sa, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port = (u16)(prb_cfg.port[0] | prb_cfg.port[1] << 8);       /* already network order */
    sa.sin_addr.s_addr = (u32)prb_cfg.ip[0] | (u32)prb_cfg.ip[1] << 8 | (u32)prb_cfg.ip[2] << 16 | (u32)prb_cfg.ip[3] << 24;
    for (int tries = 0; tries < 10; tries++) {
        int fd = sceNetSocket("prb", AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        if (sceNetConnect(fd, (struct sockaddr *)&sa, sizeof sa) >= 0) {
            int one = 1;
            sceNetSetsockopt(fd, 6 /* IPPROTO_TCP */, TCP_NODELAY, &one, sizeof one);
            return fd;
        }
        sceNetSocketClose(fd);
        sceKernelUsleep(1000000);
    }
    return -1;
}

static void sysctl_rec(const char *name, u64 *val_out)
{
    size_t len = PRB_SCRATCH;
    int r = sysctlbyname((char *)name, (char *)g_scratch, &len, 0, 0);
    if (r < 0) len = 0;
    if (val_out && r >= 0 && len >= 4) { *val_out = 0; prb_copy(val_out, g_scratch, len >= 8 ? 8 : 4); }
    u32 nl = prb_strlen(name) + 1;
    u8 *d = rec_begin(C, T_SYSCTL, (u64)(s64)r, len, nl + (u32)len);
    if (d) { prb_copy(d, name, nl); prb_copy(d + nl, g_scratch, (u32)len); }
}

static void clock_rec(u32 sample) { rec(C, T_CLOCK, sample, sceKernelGetProcessTime() * 1000ull, 0, 0); }

static const char phase_name[PH_LAST + 1][8] = { "", "fw", "cpu", "tsc", "pci", "sb", "irq", "gpu", "acpi", "dev", "mem" };

int _main(struct thread *td)
{
    UNUSED(td);
    const struct prb_adapter *ad = adapter_for(prb_cfg.env);
    if (!ad) return 1;                    /* undeclared environment: do nothing (see adapters.h) */

    initKernel();
    initLibc();
    initNetwork();

    g_fd = connect_pc();
    if (g_fd < 0) return 2;

    u8 *buf = mmap(0, PRB_BUF_SIZE, 3 /* RW */, 0x1002 /* ANON|PRIVATE */, -1, 0);
    if (!buf || buf == (u8 *)-1) { sceNetSocketClose(g_fd); return 3; }
    for (u32 o = 0; o < PRB_BUF_SIZE; o += 0x1000) buf[o] = 0;
    int lk = mlock(buf, PRB_BUF_SIZE);

    C = (struct prb_ctx *)buf;
    prb_zero(C, sizeof *C);
    C->magic = PRB_CTX_MAGIC;
    C->out = buf + 0x10000;
    C->cap = PRB_BUF_SIZE - 0x10000 - PRB_SCRATCH;
    g_scratch = buf + PRB_BUF_SIZE - PRB_SCRATCH;
    prb_copy(&C->cfg, &prb_cfg, sizeof prb_cfg);
    C->id.env = prb_cfg.env; C->id.level = prb_cfg.level; C->id.strict = prb_cfg.strict;

    send_all(PRB_MAGIC, 8);
    rec(C, T_HELLO, PRB_VERSION, prb_cfg.env, 0, 0);
    rec_s(C, T_NOTE, (u64)(s64)lk, 0, "mlock");
    diag(C, D_ENV, ST_OK, ad->name);
    flush(0);

    sysctl_rec("kern.ostype", 0);
    sysctl_rec("kern.osrelease", 0);
    sysctl_rec("kern.version", 0);
    sysctl_rec("kern.sdk_version", 0);
    sysctl_rec("hw.model", 0);
    sysctl_rec("hw.ncpu", 0);
    sysctl_rec("hw.physmem", 0);
    sysctl_rec("hw.realmem", 0);
    sysctl_rec("machdep.smap", 0);
    sysctl_rec("vm.phys_segs", 0);
    sysctl_rec("machdep.acpi_root", &C->acpi_root);
    diag(C, D_MEM_SYSCTL, ST_OK, "machdep.smap, vm.phys_segs, hw.physmem (status per sysctl in T_SYSCTL.a)");

    u16 v = get_firmware();                         /* SDK: libc.sprx SCE header, sandbox-aware */
    if (v >= 100 && v < 2000) C->fw_user = v;
    C->id.fw_user = C->fw_user;
    rec(C, T_FW, FWSRC_LIBC, C->fw_user, 0, 0);
    if (prb_cfg.fw_hint) rec(C, T_FW, FWSRC_HINT, prb_cfg.fw_hint, 0, 0);
    diag(C, D_FW_USER, C->fw_user ? ST_OK : ST_FAILED, C->fw_user ? "version from libc.sprx" : "libc.sprx version not readable");

    u32 a = 7, b, cc = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(cc), "=d"(d));
    int smx = (b >> 7 & 1) | (b >> 20 & 1);
    diag(C, D_SMEP, smx ? ST_SKIPPED_SAFETY : ST_OK, smx ? "SMEP/SMAP present: ring0 would run from user pages" : "no SMEP/SMAP");
    u32 knr = adapter_kexec_nr(ad, C->fw_user ? C->fw_user : prb_cfg.fw_hint);
    rec_s(C, T_NOTE, knr, C->fw_user, "kexec syscall | version");
    diag(C, D_KEXEC, knr ? ST_OK : ST_SKIPPED_SAFETY,
         knr ? "loader's kexec syscall known for this version" : "this loader installs no known kexec on this version: no ring0");
    flush(0);

    if (!smx && knr) {
        for (u32 ph = PH_FW; ph <= PH_LAST; ph++) {
            if (ph == PH_TSC) { clock_rec(0); sceKernelUsleep(200000); clock_rec(1); }
            C->phase = ph;
            rec_s(C, T_PHASE, ph, 0, phase_name[ph]);
            if (flush(150) < 0) break;              /* evidence first, then ring0 */
            int r = (int)kexec_nr(knr, (void *)prb_ring0, C);
            rec(C, T_RET, ph, (u64)(s64)r, 0, 0);
            rec_s(C, T_PHASE, ph, 1, phase_name[ph]);
            if (flush(0) < 0) break;
        }
    }

    rec(C, T_IDENT, 0, 0, &C->id, sizeof C->id);
    rec(C, T_END, C->nrec, C->flags | (u64)C->ok << 8, 0, 0);
    flush(300);
    sceNetSocketClose(g_fd);
    munmap(buf, PRB_BUF_SIZE);
    return 0;
}
