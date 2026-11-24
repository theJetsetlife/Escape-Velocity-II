/* Generic non-UEFI diagnostic .bin for the existing PS4 WebKitty loader.
 * The source intentionally does not contain protected-key extraction logic.
 * PC receiver: tools/diag_recv.py listen --port 9031
 */
#include "native.h"

/* FreeBSD/Orbis amd64 syscall ABI; WebKitty's existing loader establishes
 * the runPayload userland execution context. */
static long sys6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    long result = n;
    uint8_t carry;
    __asm__ volatile("syscall; setc %1"
        : "+a"(result), "=r"(carry), "+D"(a1), "+S"(a2), "+d"(a3),
          "+r"(r10), "+r"(r8), "+r"(r9)
        :: "rcx", "r11", "memory", "cc");
    return carry ? -result : result;
}

#define SYS_WRITE 4
#define SYS_CLOSE 6
#define SYS_SOCKET 97
#define SYS_CONNECT 98

struct record_header {
    uint32_t tag;
    uint32_t length;
    uint64_t a;
    uint64_t b;
};
_Static_assert(sizeof(struct record_header) == 24, "receiver record header ABI");
_Static_assert(sizeof(struct config_block) == 16, "blob patch config ABI");

static long report_fd = -1;
static uint64_t report_count;

static int send_all(const void *bytes, uint64_t length)
{
    const uint8_t *cursor = bytes;
    while (length) {
        long sent = sys6(SYS_WRITE, report_fd, (long)cursor, (long)length,
                         0, 0, 0);
        if (sent <= 0) return -1;
        cursor += sent;
        length -= (uint64_t)sent;
    }
    return 0;
}

static uint32_t note_length(const char *s)
{
    uint32_t length = 0;
    if (!s) return 0;
    while (length < 128u && s[length]) ++length;
    return length;
}

static int send_record(uint32_t tag, uint64_t a, uint64_t b,
                       const void *body, uint32_t length)
{
    const struct record_header record = {tag, length, a, b};
    static const uint8_t zeros[8] = {0};
    uint32_t padding = (8u - (length & 7u)) & 7u;
    if (report_fd < 0 || length > 128u) return -1;
    if (send_all(&record, sizeof(record)) != 0) return -1;
    if (length && (!body || send_all(body, length) != 0)) return -1;
    if (padding && send_all(zeros, padding) != 0) return -1;
    ++report_count;
    return 0;
}

int research_stage_note(const char *message)
{
    if (!message) return -1;
    return send_record(11, 0, 0, message, note_length(message)); /* T_NOTE */
}

static long connect_receiver(void)
{
    uint8_t address[16] = {0};
    address[0] = 16; /* FreeBSD sockaddr_in sin_len */
    address[1] = 2; /* AF_INET */
    address[2] = scaffold_cfg.port[0];
    address[3] = scaffold_cfg.port[1];
    for (int i = 0; i < 4; i++) address[4+i] = scaffold_cfg.ip[i];
    long fd = sys6(SYS_SOCKET, 2, 1, 0, 0, 0, 0);
    if (fd < 0) return fd;
    if (sys6(SYS_CONNECT, fd, (long)address, sizeof(address),
             0, 0, 0) < 0) {
        sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        return -1;
    }
    return fd;
}

int scaffold_main(void)
{
    if (scaffold_cfg.magic[0] != 'D' || scaffold_cfg.magic[1] != 'B') return 1;
    report_fd = connect_receiver();
    if (report_fd < 0) return 2;
    if (send_all("DBDIAG01", 8) != 0) goto cleanup;
    if (send_record(1, 1, 1, 0, 0) != 0) goto cleanup; /* HELLO version 1 */
    if (research_stage_note("native research scaffold: started") != 0) goto cleanup;
    if (send_record(2, 1, 0, "extension", 9) != 0) goto cleanup; /* STAGE enter */
    int result = research_stage_run();
    if (send_record(2, 1, 1, "extension", 9) != 0) goto cleanup; /* STAGE leave */
    if (result == RESEARCH_STAGE_UNIMPLEMENTED) {
        if (research_stage_note("user research extension: NOT IMPLEMENTED") != 0)
            goto cleanup;
    }
    /* END: a = number of preceding records, b = zero diagnostic flags. */
    if (send_record(13, report_count, 0, 0, 0) != 0)
        goto cleanup;
    sys6(SYS_CLOSE, report_fd, 0, 0, 0, 0, 0);
    report_fd = -1;
    return 0;
cleanup:
    sys6(SYS_CLOSE, report_fd, 0, 0, 0, 0, 0);
    report_fd = -1;
    return 3;
}
