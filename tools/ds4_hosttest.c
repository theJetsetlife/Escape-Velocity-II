/* ds4_hosttest.c - host unit test for Ps4Pkg/Ps4Ds4Dxe/Ds4Report.c (DualShock 4 USB report 0x01).
 * Build: cc -std=c11 -Wall -Wextra -Werror -DDS4_HOST_TEST -I../Ps4Pkg/Ps4Ds4Dxe \
 *          -o ds4_hosttest ds4_hosttest.c ../Ps4Pkg/Ps4Ds4Dxe/Ds4Report.c
 * Reports are synthesized from the layout in PS4 Linux hid-playstation.c (no captured pad data). */
#include <stdio.h>
#include <string.h>
#include "Ds4Report.h"

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void report(UINT8 *r, UINT8 b5, UINT8 b6, UINT8 b7)
{
    memset(r, 0, DS4_REPORT_SIZE);
    r[0] = DS4_REPORT_ID; r[1] = r[2] = r[3] = r[4] = 0x80;   /* sticks centred */
    r[5] = b5; r[6] = b6; r[7] = b7;
}

int main(void)
{
    UINT8 r[DS4_REPORT_SIZE];
    UINT32 b;

    /* hat: every value 0..15 */
    static const UINT32 want_hat[16] = {
        DS4_BIT(Ds4Up), DS4_BIT(Ds4Up) | DS4_BIT(Ds4Right), DS4_BIT(Ds4Right), DS4_BIT(Ds4Right) | DS4_BIT(Ds4Down),
        DS4_BIT(Ds4Down), DS4_BIT(Ds4Down) | DS4_BIT(Ds4Left), DS4_BIT(Ds4Left), DS4_BIT(Ds4Left) | DS4_BIT(Ds4Up),
        0, 0, 0, 0, 0, 0, 0, 0 };
    for (UINT8 h = 0; h < 16; h++) {
        report(r, h, 0, 0); b = 0xdeadbeef;
        CHECK(Ds4ParseReport(r, sizeof r, FALSE, &b), "hat %u parse", h);
        CHECK(b == want_hat[h], "hat %u -> %#x want %#x", h, b, want_hat[h]);
    }

    /* every button bit alone (hat released = 8) */
    struct { int byte; UINT8 bit; DS4_BUTTON btn; } bits[] = {
        {5, 0x10, Ds4Square}, {5, 0x20, Ds4Cross}, {5, 0x40, Ds4Circle}, {5, 0x80, Ds4Triangle},
        {6, 0x01, Ds4L1}, {6, 0x02, Ds4R1}, {6, 0x04, Ds4L2}, {6, 0x08, Ds4R2},
        {6, 0x10, Ds4Share}, {6, 0x20, Ds4Options}, {6, 0x40, Ds4L3}, {6, 0x80, Ds4R3},
        {7, 0x01, Ds4Ps}, {7, 0x02, Ds4Touchpad} };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        report(r, 8, 0, 0); r[bits[i].byte] |= bits[i].bit;
        CHECK(Ds4ParseReport(r, sizeof r, FALSE, &b) && b == DS4_BIT(bits[i].btn),
              "byte %d bit %#x -> %#x want %#x", bits[i].byte, bits[i].bit, b, DS4_BIT(bits[i].btn));
    }

    /* everything at once */
    report(r, 0x08 | 0xf0, 0xff, 0x03);
    CHECK(Ds4ParseReport(r, sizeof r, FALSE, &b) && b == ((DS4_BIT(Ds4ButtonCount) - 1) & ~(DS4_BIT(Ds4Up) | DS4_BIT(Ds4Down) | DS4_BIT(Ds4Left) | DS4_BIT(Ds4Right))),
          "all buttons -> %#x", b);
    /* frame-counter bits in byte 7 (bits 2-7) are ignored */
    report(r, 8, 0, 0xfc);
    CHECK(Ds4ParseReport(r, sizeof r, FALSE, &b) && b == 0, "byte 7 counter bits -> %#x", b);

    /* rejects: wrong id, short, NULL */
    report(r, 0, 0, 0); r[0] = 0x11; b = 0x55;
    CHECK(!Ds4ParseReport(r, sizeof r, FALSE, &b) && b == 0x55, "report 0x11 must be rejected");
    report(r, 0, 0, 0); b = 0x55;
    CHECK(!Ds4ParseReport(r, 7, FALSE, &b) && b == 0x55, "short report must be rejected");
    CHECK(!Ds4ParseReport(NULL, 64, FALSE, &b), "NULL report must be rejected");

    /* dongle: byte 31 bit 2 = no pad -> no buttons; same bit ignored for a cabled pad */
    report(r, 0x28, 0, 0); r[31] = 0x04;                     /* hat released (8) + Cross */
    CHECK(Ds4ParseReport(r, sizeof r, TRUE, &b) && b == 0, "dongle without pad -> %#x", b);
    CHECK(Ds4ParseReport(r, sizeof r, FALSE, &b) && b == DS4_BIT(Ds4Cross), "cabled pad ignores byte 31 -> %#x", b);

    /* key mapping */
    DS4_KEY k;
    CHECK(Ds4ButtonKey(Ds4Cross, &k) && k.UnicodeChar == CHAR_CARRIAGE_RETURN && k.ScanCode == SCAN_NULL, "Cross = Enter");
    CHECK(Ds4ButtonKey(Ds4Circle, &k) && k.ScanCode == SCAN_ESC, "Circle = Esc");
    CHECK(Ds4ButtonKey(Ds4Options, &k) && k.ScanCode == SCAN_F2, "Options = F2");
    CHECK(Ds4ButtonKey(Ds4Share, &k) && k.ScanCode == SCAN_F1, "Share = F1");
    CHECK(Ds4ButtonKey(Ds4Up, &k) && k.ScanCode == SCAN_UP, "Up");
    CHECK(Ds4ButtonKey(Ds4Triangle, &k) && k.UnicodeChar == CHAR_BACKSPACE, "Triangle = Backspace");
    CHECK(Ds4ButtonKey(Ds4Square, &k) && k.UnicodeChar == ' ', "Square = Space");
    CHECK(!Ds4ButtonKey(Ds4L2, &k) && !Ds4ButtonKey(Ds4R3, &k), "L2/R3 have no key");
    CHECK(!Ds4ButtonKey(Ds4ButtonCount, &k), "out of range");
    CHECK(Ds4ButtonRepeats(Ds4Left) && !Ds4ButtonRepeats(Ds4Cross), "repeat set");

    /* edges: only newly pressed buttons produce keys, in DS4_BUTTON order */
    DS4_KEY keys[8];
    UINTN n = Ds4NewKeys(DS4_BIT(Ds4Cross), DS4_BIT(Ds4Cross) | DS4_BIT(Ds4Up) | DS4_BIT(Ds4L2), keys, 8);
    CHECK(n == 1 && keys[0].ScanCode == SCAN_UP, "held Cross + new Up + new L2 -> 1 key (Up), got %zu", n);
    n = Ds4NewKeys(DS4_BIT(Ds4Up), 0, keys, 8);
    CHECK(n == 0, "release gives no key");
    n = Ds4NewKeys(0, DS4_BIT(Ds4ButtonCount) - 1, keys, 3);
    CHECK(n == 3 && keys[0].ScanCode == SCAN_UP && keys[1].ScanCode == SCAN_DOWN && keys[2].ScanCode == SCAN_LEFT, "Max respected, order kept");

    printf("RESULT: %s (%d checks, %d failed)\n", g_fail ? "FAIL" : "all checks passed", g_checks, g_fail);
    return g_fail != 0;
}
