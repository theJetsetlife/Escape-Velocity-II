/* Userland half of the read-only diagnostic payload.
 *
 * Launch ABI (WebKitty psfree-lapse runPayload, lapse.mjs): the raw blob is copied into
 * an RWX anonymous mapping and called with no arguments from the WebKit process,
 * after kpatch has (a) installed sysent[11] = jmp [rsi] and (b) disabled the
 * "syscall must come from libkernel" check, so raw `syscall` instructions work here.
 * Syscall numbers: WebKitty module/chain.mjs syscall_map (exploit source).
 *
 * Order of operations (each step reported to the PC before the next one starts):
 *   connect -> HELLO -> userland sysctls -> for each phase: STAGE enter, flush,
 *   kexec(ring0_main), RET, flush.  No reporting channel => no ring0 at all.
 */
#include "rec.h"

struct diag_cfg { char magic[8]; u8 ip[4]; u8 port[2]; u16 rsvd; };
extern struct diag_cfg diag_cfg;               /* entry.S, patchable in the blob */
int ring0_main(void *td, u64 *uap);

/* ---- libc-free basics ------------------------------------------------------ */
void *memset(void *d, int c, size_t n)
{
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
void *memcpy(void *d, const void *s, size_t n)
{
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return r;
}

/* FreeBSD amd64 syscall: args rdi rsi rdx r10 r8 r9, error = CF set, rax = errno. */
static long sys6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    long rax = n; u8 cf;
    __asm__ volatile("syscall; setc %1"
                     : "+a"(rax), "=r"(cf), "+D"(a1), "+S"(a2), "+d"(a3),
                       "+r"(r10), "+r"(r8), "+r"(r9)
                     :: "rcx", "r11", "memory", "cc");
    return cf ? -rax : rax;
}
#define SYS_write      4
#define SYS_close      6
#define SYS_munmap     73
#define SYS_socket     97
#define SYS_connect    98
#define SYS_setsockopt 105
#define SYS___sysctl   202
#define SYS_mlock      203
#define SYS_nanosleep  240
#define SYS_mmap       477
#define SYS_kexec      11

static void msleep(long ms)
{
    long ts[2] = { ms / 1000, (ms % 1000) * 1000000L };
    sys6(SYS_nanosleep, (long)ts, 0, 0, 0, 0, 0);
}

/* ---- transport ------------------------------------------------------------- */
struct io { struct diag_ctx *c; long fd; };

static int send_all(long fd, const void *p, u64 n)
{
    const u8 *b = p;
    while (n) {
        long w = sys6(SYS_write, fd, (long)b, (long)n, 0, 0, 0);
        if (w <= 0) return -1;
        b += w; n -= (u64)w;
    }
    return 0;
}

/* Send what is buffered and reuse the buffer. linger_ms gives the NIC time to put
 * it on the wire before something that could take the console down. */
static int flush(struct io *io, long linger_ms)
{
    int r = send_all(io->fd, io->c->out, io->c->len);
    io->c->len = 0;
    if (linger_ms) msleep(linger_ms);
    return r;
}

static long connect_pc(void)
{
    u8 sa[16];
    memset(sa, 0, sizeof sa);
    sa[0] = 16; sa[1] = 2;                       /* sin_len, AF_INET */
    sa[2] = diag_cfg.port[0]; sa[3] = diag_cfg.port[1];
    memcpy(sa + 4, diag_cfg.ip, 4);
    for (int tries = 0; tries < 10; tries++) {
        long fd = sys6(SYS_socket, 2, 1, 0, 0, 0, 0);   /* AF_INET, SOCK_STREAM */
        if (fd < 0) return fd;
        if (sys6(SYS_connect, fd, (long)sa, 16, 0, 0, 0) == 0) {
            int one = 1;                                 /* IPPROTO_TCP, TCP_NODELAY */
            sys6(SYS_setsockopt, fd, 6, 1, (long)&one, 4, 0);
            return fd;
        }
        sys6(SYS_close, fd, 0, 0, 0, 0, 0);
        msleep(1000);
    }
    return -1;
}

/* ---- sysctl (userland view of memory map / ACPI root) ------------------------ */
static u8 *scratch;                 /* 64 KiB at the end of the buffer */
#define SCRATCH 0x10000u

static long sysctl_name(const char *name, void *out, u64 *olen)
{
    int mib[2] = { 0, 3 };          /* sysctl.name2oid */
    int oid[24]; u64 len = sizeof oid;
    long r = sys6(SYS___sysctl, (long)mib, 2, (long)oid, (long)&len,
                  (long)name, dstrlen(name));
    if (r < 0) return r;
    return sys6(SYS___sysctl, (long)oid, (long)(len / 4), (long)out, (long)olen, 0, 0);
}

static void sysctl_rec(struct diag_ctx *c, const char *name, u64 *val_out)
{
    u64 len = SCRATCH;
    long r = sysctl_name(name, scratch, &len);
    if (r < 0) len = 0;
    if (val_out && r >= 0 && len >= 4) {
        *val_out = 0;
        memcpy(val_out, scratch, len >= 8 ? 8 : 4);
    }
    u32 nl = dstrlen(name) + 1;
    u8 *d = rec_begin(c, T_SYSCTL, (u64)r, len, nl + (u32)len);
    if (d) { memcpy(d, name, nl); memcpy(d + nl, scratch, len); }
}

static long diag_kexec(void *fn, void *arg)
{
    return sys6(SYS_kexec, (long)fn, (long)arg, 0, 0, 0, 0);
}

/* char arrays, not pointers: the blob is position independent and has no relocator */
static const char phase_name[PH_LAST + 1][8] = {
    "", "ident", "cpu", "pgtbl", "pci", "glue", "iommu", "gpu", "acpi", "dmap",
};

/* ---- main ------------------------------------------------------------------ */
int diag_main(void)
{
    long fd = connect_pc();
    if (fd < 0) return 1;                        /* no channel -> do nothing privileged */

    long m = sys6(SYS_mmap, 0, DIAG_BUF_SIZE, 3 /* RW */, 0x1002 /* ANON|PRIVATE */, -1, 0);
    if (m < 0 && m > -4096) { sys6(SYS_close, fd, 0, 0, 0, 0, 0); return 2; }
    u8 *buf = (u8 *)m;
    for (u32 o = 0; o < DIAG_BUF_SIZE; o += 0x1000) buf[o] = 0;   /* fault every page in */
    long lk = sys6(SYS_mlock, (long)buf, DIAG_BUF_SIZE, 0, 0, 0, 0);

    struct diag_ctx *c = (struct diag_ctx *)buf;
    memset(c, 0, sizeof *c);
    c->magic = DIAG_CTX_MAGIC;
    c->out = buf + 0x1000;
    c->cap = DIAG_BUF_SIZE - 0x1000 - SCRATCH;
    scratch = buf + DIAG_BUF_SIZE - SCRATCH;
    struct io io = { c, fd };

    send_all(fd, DIAG_MAGIC, 8);
    rec(c, T_HELLO, DIAG_VERSION, 1, 0, 0);
    rec_s(c, T_NOTE, (u64)lk, 0, "mlock");
    flush(&io, 0);

    /* stage 1 proof: userland executes. Now the userland view. */
    sysctl_rec(c, "kern.ostype", 0);
    sysctl_rec(c, "kern.osrelease", 0);
    sysctl_rec(c, "kern.version", 0);
    sysctl_rec(c, "hw.physmem", 0);
    sysctl_rec(c, "hw.realmem", 0);
    sysctl_rec(c, "hw.usermem", 0);
    sysctl_rec(c, "hw.ncpu", 0);
    sysctl_rec(c, "machdep.smap", 0);
    sysctl_rec(c, "vm.phys_segs", 0);
    sysctl_rec(c, "vm.phys_free", 0);
    sysctl_rec(c, "machdep.acpi_root", &c->acpi_root);
    sysctl_rec(c, "hw.acpi.supported_sleep_state", 0);

    /* CPUID is unprivileged: refuse ring0 if SMEP/SMAP exist (ring0 runs from and
     * writes to user pages). LEDGER H4: leaf7 EBX=0x08 -> neither on the Fat. */
    u32 a = 7, b, cc = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(cc), "=d"(d));
    int smx = (b >> 7 & 1) | (b >> 20 & 1);
    rec_s(c, T_GATE, 100, !smx, "CPU has no SMEP/SMAP");
    flush(&io, 0);

    if (!smx) {
        for (u32 ph = PH_IDENT; ph <= PH_LAST; ph++) {
            c->phase = ph;
            rec_s(c, T_STAGE, ph, 0, phase_name[ph]);
            if (flush(&io, 150) < 0) break;               /* evidence first, then ring0 */
            long r = diag_kexec((void *)ring0_main, c);
            rec(c, T_RET, ph, (u64)r, 0, 0);
            if (r != 0) rec(c, T_ERR, E_KEXEC, (u64)r, 0, 0);
            rec_s(c, T_STAGE, ph, 1, phase_name[ph]);
            if (flush(&io, 0) < 0) break;
            if (ph == PH_IDENT && !(c->ok & (1u << PH_IDENT))) break;  /* gate failed */
        }
    }

    rec(c, T_END, c->nrec, c->flags | (u64)c->ok << 8, 0, 0);
    flush(&io, 300);
    sys6(SYS_close, fd, 0, 0, 0, 0, 0);
    sys6(SYS_munmap, (long)buf, DIAG_BUF_SIZE, 0, 0, 0, 0);
    return 0;
}
