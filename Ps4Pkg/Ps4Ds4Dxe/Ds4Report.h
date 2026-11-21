/** @file
  Ds4Report - DualShock 4 USB input report 0x01 -> buttons -> UEFI keys. Pure C, no UEFI services,
  so tools/ds4_hosttest.c can run it on the build host (DS4_HOST_TEST).

  Layout (PS4 Linux hid-playstation.c:98-112, 454-494): byte 0 = 0x01; byte 5 bits 3:0 hat
  (0 Up, 1 Up-Right, 2 Right, 3 Down-Right, 4 Down, 5 Down-Left, 6 Left, 7 Up-Left, 8 none),
  bit 4 Square, 5 Cross, 6 Circle, 7 Triangle; byte 6 bit 0 L1, 1 R1, 2 L2, 3 R2, 4 Share,
  5 Options, 6 L3, 7 R3; byte 7 bit 0 PS, bit 1 touchpad click. Wireless dongle 054c:0ba0:
  byte 31 bit 2 = no pad connected.
**/

#ifndef DS4_REPORT_H_
#define DS4_REPORT_H_

#ifdef DS4_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef size_t    UINTN;
typedef uint16_t  CHAR16;
typedef uint8_t   BOOLEAN;
#define TRUE   1
#define FALSE  0
#define CONST  const
#define IN
#define OUT
#define SCAN_NULL       0x0000
#define SCAN_UP         0x0001
#define SCAN_DOWN       0x0002
#define SCAN_RIGHT      0x0003
#define SCAN_LEFT       0x0004
#define SCAN_HOME       0x0005
#define SCAN_PAGE_UP    0x0009
#define SCAN_PAGE_DOWN  0x000A
#define SCAN_F1         0x000B
#define SCAN_F2         0x000C
#define SCAN_ESC        0x0017
#define CHAR_NULL             0x0000
#define CHAR_BACKSPACE        0x0008
#define CHAR_TAB              0x0009
#define CHAR_CARRIAGE_RETURN  0x000D
#else
#include <Uefi.h>
#endif

#define DS4_REPORT_ID    0x01
#define DS4_REPORT_SIZE  64

typedef enum {
  Ds4Up = 0,
  Ds4Down,
  Ds4Left,
  Ds4Right,
  Ds4Cross,
  Ds4Circle,
  Ds4Square,
  Ds4Triangle,
  Ds4L1,
  Ds4R1,
  Ds4L2,
  Ds4R2,
  Ds4Share,
  Ds4Options,
  Ds4L3,
  Ds4R3,
  Ds4Ps,
  Ds4Touchpad,
  Ds4ButtonCount
} DS4_BUTTON;

#define DS4_BIT(Button)  ((UINT32)1 << (Button))

typedef struct {
  UINT16    ScanCode;
  CHAR16    UnicodeChar;
} DS4_KEY;

/** Decode one report into a DS4_BIT() mask. FALSE (Buttons untouched) when it is not report 0x01
    or too short. A dongle without a pad gives Buttons = 0. */
BOOLEAN
Ds4ParseReport (
  IN  CONST UINT8  *Report,
  IN  UINTN        Length,
  IN  BOOLEAN      IsDongle,
  OUT UINT32       *Buttons
  );

/** UEFI key for a button. FALSE when the button has no key (L2, R2, L3, R3). */
BOOLEAN
Ds4ButtonKey (
  IN  UINTN    Button,
  OUT DS4_KEY  *Key
  );

/** TRUE for buttons that auto-repeat while held (directions, page keys, Backspace). */
BOOLEAN
Ds4ButtonRepeats (
  IN UINTN  Button
  );

/** Keys for the buttons pressed between Old and New (rising edges), in DS4_BUTTON order.
    Returns the number written to Keys (at most Max). */
UINTN
Ds4NewKeys (
  IN  UINT32   Old,
  IN  UINT32   New,
  OUT DS4_KEY  *Keys,
  IN  UINTN    Max
  );

#endif
