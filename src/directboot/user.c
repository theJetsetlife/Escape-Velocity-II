/* Directboot stage — userland half (WebKit process, GoldHEN slot; same launch ABI as diag.bin,
 * LEDGER A7/A10). Log stream = the diag record format to the PC receiver (proven on HW).
 *
 *   1. connect log channel, HELLO (version 0x100 = directboot)
 *   2. second connection: "DBUPLGET" -> u64 size + upl-ps4.elf
 *   3. clock_gettime(CLOCK_REALTIME) for the EDK II RTC seed
 *   4. kexec(db_arm) -> arms the shutdown hook
 *   5. tell the operator to restart the console (Power > Restart PS4)
 */
#include "db.h"

struct db_cfg { char magic[8]; u8 ip[4]; u8 port[2]; u16 rsvd; };
extern struct db_cfg db_cfg;

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
#define SYS_read        3
#define SYS_write       4
#define SYS_close       6
#define SYS_munmap      73
#define SYS_socket      97
#define SYS_connect     98
#define SYS_setsockopt  105
#define SYS_mlock       203
#define SYS_clock_gettime 232
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
static int recv_all(long fd, void *p, u64 n)
{
    u8 *b = p;
    while (n) {
        long r = sys6(SYS_read, fd, (long)b, (long)(n > 0x100000 ? 0x100000 : n), 0, 0, 0);
        if (r <= 0) return -1;
        b += r; n -= (u64)r;
    }
    return 0;
}

static long connect_pc(void)
{
    u8 sa[16];
    memset(sa, 0, sizeof sa);
    sa[0] = 16; sa[1] = 2;
    sa[2] = db_cfg.port[0]; sa[3] = db_cfg.port[1];
    memcpy(sa + 4, db_cfg.ip, 4);
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

static u8 *map_locked(u64 size)
{
    long m = sys6(SYS_mmap, 0, (long)size, 3, 0x1002, -1, 0);
    if (m < 0 && m > -4096) return 0;
    u8 *p = (u8 *)m;
    for (u64 o = 0; o < size; o += 0x1000) p[o] = 0;
    sys6(SYS_mlock, (long)p, (long)size, 0, 0, 0, 0);
    return p;
}

static int flush(long fd, struct diag_ctx *c)
{
    int r = send_all(fd, c->out, c->len);
    c->len = 0;
    return r;
}

#define LOG_SIZE  (256u << 10)
#define UPL_MAX   (96ull << 20)

int db_main(void)
{
    long fd = connect_pc();
    if (fd < 0) return 1;

    u8 *lb = map_locked(LOG_SIZE);
    if (!lb) { sys6(SYS_close, fd, 0, 0, 0, 0, 0); return 2; }
    struct diag_ctx *c = (struct diag_ctx *)lb;
    memset(c, 0, sizeof *c);
    c->magic = DIAG_CTX_MAGIC; c->out = lb + 0x1000; c->cap = LOG_SIZE - 0x1000;

    send_all(fd, DIAG_MAGIC, 8);
    rec(c, T_HELLO, 0x100, 1, 0, 0);
    rec_s(c, T_STAGE, 1, 0, "fetch upl-ps4.elf");
    flush(fd, c);

    /* ---- fetch the payload over a second connection ---- */
    u64 size = 0; u8 *elf = 0;
    long uf = connect_pc();
    if (uf >= 0 && send_all(uf, "DBUPLGET", 8) == 0 && recv_all(uf, &size, 8) == 0 &&
        size >= 64 && size <= UPL_MAX && (elf = map_locked((size + 0xfff) & ~0xfffull)) &&
        recv_all(uf, elf, size) == 0) {
        rec_s(c, T_NOTE, size, ((u32 *)elf)[0], "upl received: size|magic");
    } else {
        rec_s(c, T_ERR, 20, size, "could not fetch upl-ps4.elf from the PC");
        size = 0;
    }
    if (uf >= 0) sys6(SYS_close, uf, 0, 0, 0, 0, 0);

    long ts[2] = { 0, 0 };
    sys6(SYS_clock_gettime, 0 /* CLOCK_REALTIME */, (long)ts, 0, 0, 0, 0);
    rec_s(c, T_NOTE, (u64)ts[0], 0, "orbis unix time");

    /* TSC frequency over a 200 ms sleep (EDK II uses the Linux-measured 1593.719 MHz;
     * this logs ours so a mismatch is visible) */
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    u64 t0 = ((u64)hi << 32) | lo;
    msleep(200);
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    u64 tsc_hz = ((((u64)hi << 32) | lo) - t0) * 5;
    rec_s(c, T_NOTE, tsc_hz, 0, "tsc hz (200 ms sample)");

    struct db_arm_args a;
    memset(&a, 0, sizeof a);
    a.magic = DB_STATE_MAGIC; a.log = c; a.elf = elf; a.elf_size = size;
    a.unix_time = (u64)ts[0]; a.tsc_hz = tsc_hz; a.result = -100;

    if (size && elf && ((u32 *)elf)[0] == 0x464c457fu) {
        /* staged arm: each step is flushed to the PC before the next starts, so a hang names
         * its step (receiver: "== phase 2x <name>: ENTER" with no LEAVE) */
        static const char stepname[DB_ARM_STEPS][28] = {
            "arm0 checks (read-only)", "arm1 text patches", "arm2 kernel copy alloc",
            "arm3 scanout geometry", "arm4 contig elf", "arm5 contig region", "arm6 contig boot",
            "arm7 contig fb", "arm8 extract + elf copy", "arm9 finish" };
        a.result = 0;
        for (u32 st = 0; st < DB_ARM_STEPS && a.result == 0; st++) {
            a.arm_step = st;
            rec_s(c, T_STAGE, 20 + st, 0, stepname[st]);
            flush(fd, c);
            msleep(100);
            long r = sys6(SYS_kexec, (long)db_arm, (long)&a, 0, 0, 0, 0);
            /* allocation steps never wait in the kernel (run 05:50); retry from here instead */
            for (u32 retry = 1; a.result == DB_ARM_RETRY && retry <= 40; retry++) {
                rec_s(c, T_NOTE, st, retry, "contig busy - retrying step in 250 ms (step|retry)");
                flush(fd, c);
                msleep(250);
                r = sys6(SYS_kexec, (long)db_arm, (long)&a, 0, 0, 0, 0);
            }
            if (a.result == DB_ARM_RETRY) {
                rec_s(c, T_ERR, 8, st, "contiguous memory not available without waiting - NOT armed (console left running)");
                a.result = -8;
            }
            rec(c, T_RET, 20 + st, (u64)r, 0, 0);
            rec_s(c, T_STAGE, 20 + st, 1, stepname[st]);
            flush(fd, c);
        }
        if (a.result == 0) {
            rec_s(c, T_STAGE, 3, 0, "install hook (all CPUs stopped)");
            flush(fd, c);
            msleep(150);
            sys6(SYS_kexec, (long)db_install, (long)&a, 0, 0, 0, 0);
            rec_s(c, T_STAGE, 3, 1, "install hook");
        }
    }
    if (a.result == 0) {
        rec_s(c, T_NOTE, 0, 0, "ARMED - sync, then taking over NOW (display still live); squares top-left show progress");
        rec_s(c, T_NOTE, 2, 0, "wlanbt(2) next = wifi/bt reset (reference hook step) - the log ends here");
    } else
        rec_s(c, T_ERR, 21, (u64)a.result, "NOT armed - nothing was hooked");
    rec(c, T_END, c->nrec, (u64)(a.result == 0) << 8, 0, 0);
    flush(fd, c);
    msleep(1500);                                   /* let the log reach the PC */
    sys6(SYS_close, fd, 0, 0, 0, 0, 0);
    if (a.result == 0) {
        sys6(36 /* SYS_sync */, 0, 0, 0, 0, 0, 0);               /* flush OrbisOS disk buffers */
        msleep(2000);
        /* stop the SDIO wifi/bt DMA through OrbisOS's own driver before the IOMMU goes off
         * (reference linux_boot.c:454); drops the receiver link, so only after the last flush */
        sys6(SYS_kexec, (long)db_wlanbt, (long)&a, 0, 0, 0, 0);
        msleep(500);
        sys6(SYS_kexec, (long)db_reboot, (long)&a, 0, 0, 0, 0);   /* does not return */
    }
    if (elf) sys6(SYS_munmap, (long)elf, (long)((size + 0xfff) & ~0xfffull), 0, 0, 0, 0);
    return 0;
}
