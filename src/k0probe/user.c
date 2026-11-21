/* k0probe userland half (WebKit process, GoldHEN slot; launch ABI as diag.bin, LEDGER A7/A10).
 * Log = diag record stream to tools/diag_recv.py (raw .bin is always kept; decode with
 * tools/k0probe_decode.py). Every ring0 step is flushed to the PC before the next starts.
 * Nothing is armed, hooked or rebooted: the console stays in OrbisOS and the probe can be
 * run again in the same session. */
#include "k0.h"

struct k0_cfg { char magic[8]; u8 ip[4]; u8 port[2]; u16 rsvd; };
extern struct k0_cfg k0_cfg;

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
#define SYS_write       4
#define SYS_close       6
#define SYS_socket      97
#define SYS_connect     98
#define SYS_setsockopt  105
#define SYS_mlock       203
#define SYS_nanosleep   240
#define SYS_mmap        477
#define SYS_kexec       11

static void msleep(long ms)
{
    long ts[2] = { ms / 1000, (ms % 1000) * 1000000L };
    sys6(SYS_nanosleep, (long)ts, 0, 0, 0, 0, 0);
}

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

static long connect_pc(void)
{
    u8 sa[16];
    memset(sa, 0, sizeof sa);
    sa[0] = 16; sa[1] = 2;
    sa[2] = k0_cfg.port[0]; sa[3] = k0_cfg.port[1];
    memcpy(sa + 4, k0_cfg.ip, 4);
    for (int tries = 0; tries < 10; tries++) {
        long fd = sys6(SYS_socket, 2, 1, 0, 0, 0, 0);
        if (fd < 0) return fd;
        if (sys6(SYS_connect, fd, (long)sa, 16, 0, 0, 0) == 0) {
            int one = 1;
            sys6(SYS_setsockopt, fd, 6, 1, (long)&one, 4, 0);
            return fd;
        }
        sys6(SYS_close, fd, 0, 0, 0, 0, 0);
        msleep(1000);
    }
    return -1;
}

static int flush(long fd, struct diag_ctx *c)
{
    int r = send_all(fd, c->out, c->len);
    c->len = 0;
    return r;
}

#define LOG_SIZE (256u << 10)

int k0_main(void)
{
    long fd = connect_pc();
    if (fd < 0) return 1;
    long m = sys6(SYS_mmap, 0, LOG_SIZE, 3, 0x1002, -1, 0);
    if (m < 0 && m > -4096) { sys6(SYS_close, fd, 0, 0, 0, 0, 0); return 2; }
    u8 *lb = (u8 *)m;
    for (u64 o = 0; o < LOG_SIZE; o += 0x1000) lb[o] = 0;     /* resident before ring0 */
    sys6(SYS_mlock, (long)lb, LOG_SIZE, 0, 0, 0, 0);
    struct diag_ctx *c = (struct diag_ctx *)lb;
    memset(c, 0, sizeof *c);
    c->magic = DIAG_CTX_MAGIC; c->out = lb + 0x1000; c->cap = LOG_SIZE - 0x1000;

    /* args live in the locked buffer too (ring0 never touches pageable user memory) */
    struct k0_args *a = (struct k0_args *)(lb + 0x800);
    memset(a, 0, sizeof *a);
    a->magic = K0_MAGIC; a->log = c;

    send_all(fd, DIAG_MAGIC, 8);
    rec(c, T_HELLO, 0x200, 1, 0, 0);                         /* version 0x200 = k0probe */
    flush(fd, c);

    static const char names[K0_STEPS][32] = {
        "k0 bootstrap (user mapping)", "k0 cpu + memory MSRs", "k0 phys_avail/dump_avail",
        "k0 vm_phys segments", "k0 loader metadata (SMAP)", "k0 page translations" };
    u32 ok = 0;
    for (u32 st = 0; st < K0_STEPS; st++) {
        rec_s(c, T_STAGE, 30 + st, 0, names[st]);
        flush(fd, c);
        msleep(100);
        a->step = st; a->result = -100;
        u64 fn = st == 0 ? (u64)k0_boot : a->kblob + ((u64)k0_step - (u64)_start);
        long r = sys6(SYS_kexec, (long)fn, (long)a, 0, 0, 0, 0);
        rec(c, T_RET, 30 + st, (u64)r, 0, 0);
        rec_s(c, T_NOTE, (u64)(s64)a->result, a->tsc1 - a->tsc0, "step result | ring0 tsc ticks");
        rec_s(c, T_STAGE, 30 + st, 1, names[st]);
        flush(fd, c);
        if (a->result == 0) ok |= 1u << st;
        else if (st == 0) break;                 /* no kernel copy -> nothing else may run */
    }
    rec_s(c, T_NOTE, ok, 0, "k0probe done - read-only, nothing armed; console stays in OrbisOS");
    rec(c, T_END, c->nrec, 0, 0, 0);
    flush(fd, c);
    msleep(1500);
    sys6(SYS_close, fd, 0, 0, 0, 0, 0);
    return 0;
}
