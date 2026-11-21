/* fwsel_hosttest.c - host test of src/directboot/fwsel.c (version selection + prologue decoder).
 *   cc -O1 -Wall -Wextra -I../src -I../src/directboot fwsel_hosttest.c ../src/directboot/fwsel.c -o fwsel_hosttest
 *   ./fwsel_hosttest [kernel_dump.bin]
 * Without a dump only the decoder unit tests run. With a retail 9.00 kernel dump (flat image from the kernel
 * base; see docs/notes/LEDGER.md for its sha256) it also checks that exactly the 9.00 row matches and that
 * the decoder reproduces the 15 bytes the hardware-proven stage displaced. Kernel DATA values are not checked
 * against a dump (check_data = 0): the stage checks them on the live kernel. */
#include <stdio.h>
#include <stdlib.h>
#include "fwsel.h"
#include "fwtable.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (c) printf("  ok   "); else { printf("  FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static const char *why_name(u32 w)
{
    static const char *n[] = { "ok", "offset beyond image", "gadget", "icc already hooked", "function prologue",
                               "kmem_alloc patch1 shape", "kmem_alloc patch2 shape", "pstate shape",
                               "DMAP globals", "kernel_map", "icc prologue not decodable" };
    return w < sizeof n / sizeof n[0] ? n[w] : "?";
}

int main(int argc, char **argv)
{
    printf("prologue decoder:\n");
    static const u8 p900[32] = { 0x55, 0x48,0x89,0xe5, 0x41,0x57, 0x41,0x56, 0x41,0x54, 0x53, 0x80,0x7f,0x01,0x04, 0x90 };
    CHECK(db_prologue_len(p900, DB_HOOK_BYTES) == 15, "9.00 icc_query_nowait head -> 15 bytes");
    static const u8 riprel[32] = { 0x55, 0x48,0x89,0xe5, 0x48,0x8b,0x05,0,0,0,0, 0x53, 0x53, 0x53, 0x53 };
    CHECK(db_prologue_len(riprel, DB_HOOK_BYTES) == 0, "RIP-relative mov refused");
    static const u8 call[32] = { 0x55, 0x48,0x89,0xe5, 0xe8,0,0,0,0, 0x53,0x53,0x53,0x53,0x53,0x53 };
    CHECK(db_prologue_len(call, DB_HOOK_BYTES) == 0, "call refused");
    static const u8 subrsp[32] = { 0x55, 0x48,0x89,0xe5, 0x41,0x57, 0x53, 0x48,0x81,0xec,0x00,0x01,0x00,0x00, 0x90 };
    CHECK(db_prologue_len(subrsp, DB_HOOK_BYTES) == 14, "push/mov/sub rsp,imm32 -> 14 bytes");
    static const u8 sib[32] = { 0x55, 0x48,0x89,0xe5, 0x48,0x8b,0x44,0x24,0x08, 0x48,0x8b,0x84,0x24,0,1,0,0 };
    CHECK(db_prologue_len(sib, DB_HOOK_BYTES) == 17, "SIB disp8 + SIB disp32 forms -> 17 bytes");
    static const u8 unk[32] = { 0x55, 0x48,0x89,0xe5, 0xf3,0x0f,0x1e,0xfa, 0x53,0x53,0x53,0x53,0x53,0x53 };
    CHECK(db_prologue_len(unk, DB_HOOK_BYTES) == 0, "unknown prefix (endbr64) refused");

    printf("table:\n");
    u32 v900 = 0;
    for (u32 i = 0; i < DB_FW_COUNT; i++) {
        if (i) CHECK(db_fw_table[i].ver > db_fw_table[i - 1].ver, "rows sorted (%u after %u)", db_fw_table[i].ver, db_fw_table[i - 1].ver);
        if (db_fw_table[i].ver == 900) v900 = i + 1;
        if (db_fw_table[i].ver != 900)
            CHECK(!db_fw_table[i].phys_avail && !(db_fw_table[i].flags & DB_FW_VERIFIED), "%u.%02u unverified, no phys_avail (refused to arm)",
                  db_fw_table[i].ver / 100, db_fw_table[i].ver % 100);
    }
    CHECK(v900 && (db_fw_table[v900 - 1].flags & DB_FW_VERIFIED) && db_fw_table[v900 - 1].phys_avail == 0x2216a80,
          "9.00 verified with phys_avail");

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 2; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        u8 *kb = malloc((size_t)n + 64);
        if (!kb || fread(kb, 1, (size_t)n, f) != (size_t)n) { printf("read failed\n"); return 2; }
        fclose(f);
        printf("against %s (%ld bytes):\n", argv[1], n);
        for (u32 i = 0; i < DB_FW_COUNT; i++)
            printf("       %2u.%02u: %s\n", db_fw_table[i].ver / 100, db_fw_table[i].ver % 100,
                   why_name(db_fw_check(kb, (u64)n, &db_fw_table[i], 0)));
        u32 m, w900;
        const struct db_kofs *k = db_fw_select(kb, (u64)n, 0, &m, &w900);
        CHECK(m == 1 && k && k->ver == 900, "exactly one row matches, and it is 9.00 (matches %u, 9.00: %s)", m, why_name(w900));
        if (k) CHECK(db_prologue_len(kb + k->icc_nowait, DB_HOOK_BYTES) == 15, "icc_query_nowait in the dump decodes to 15 bytes");
    } else {
        printf("(no kernel dump given: dump checks skipped)\n");
    }
    printf("RESULT: %s (%d checks, %d failed)\n", fails ? "FAILED" : "all checks passed", checks, fails);
    return fails != 0;
}
