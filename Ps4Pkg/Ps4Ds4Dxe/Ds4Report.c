/** @file
  Ds4Report - see Ds4Report.h.
**/

#include "Ds4Report.h"

static CONST DS4_KEY  mKeys[Ds4ButtonCount] = {
  [Ds4Up]       = { SCAN_UP,        CHAR_NULL            },
  [Ds4Down]     = { SCAN_DOWN,      CHAR_NULL            },
  [Ds4Left]     = { SCAN_LEFT,      CHAR_NULL            },
  [Ds4Right]    = { SCAN_RIGHT,     CHAR_NULL            },
  [Ds4Cross]    = { SCAN_NULL,      CHAR_CARRIAGE_RETURN },
  [Ds4Circle]   = { SCAN_ESC,       CHAR_NULL            },
  [Ds4Square]   = { SCAN_NULL,      (CHAR16)' '          },
  [Ds4Triangle] = { SCAN_NULL,      CHAR_BACKSPACE       },
  [Ds4L1]       = { SCAN_PAGE_UP,   CHAR_NULL            },
  [Ds4R1]       = { SCAN_PAGE_DOWN, CHAR_NULL            },
  [Ds4Share]    = { SCAN_F1,        CHAR_NULL            },
  [Ds4Options]  = { SCAN_F2,        CHAR_NULL            },
  [Ds4Ps]       = { SCAN_HOME,      CHAR_NULL            },
  [Ds4Touchpad] = { SCAN_NULL,      CHAR_TAB             },
  // L2, R2, L3, R3: no key (all-zero entries)
};

BOOLEAN
Ds4ParseReport (
  IN  CONST UINT8  *Report,
  IN  UINTN        Length,
  IN  BOOLEAN      IsDongle,
  OUT UINT32       *Buttons
  )
{
  UINT32  B;
  UINT8   Hat;

  if ((Report == 0) || (Buttons == 0) || (Length < 8) || (Report[0] != DS4_REPORT_ID)) {
    return FALSE;
  }

  if (IsDongle && (Length >= 32) && ((Report[31] & 0x04) != 0)) {
    *Buttons = 0;                                     // dongle without a connected pad
    return TRUE;
  }

  B   = 0;
  Hat = Report[5] & 0x0F;
  if ((Hat == 7) || (Hat == 0) || (Hat == 1)) {
    B |= DS4_BIT (Ds4Up);
  }

  if ((Hat >= 1) && (Hat <= 3)) {
    B |= DS4_BIT (Ds4Right);
  }

  if ((Hat >= 3) && (Hat <= 5)) {
    B |= DS4_BIT (Ds4Down);
  }

  if ((Hat >= 5) && (Hat <= 7)) {
    B |= DS4_BIT (Ds4Left);
  }

  if (Report[5] & 0x10) { B |= DS4_BIT (Ds4Square);   }
  if (Report[5] & 0x20) { B |= DS4_BIT (Ds4Cross);    }
  if (Report[5] & 0x40) { B |= DS4_BIT (Ds4Circle);   }
  if (Report[5] & 0x80) { B |= DS4_BIT (Ds4Triangle); }
  if (Report[6] & 0x01) { B |= DS4_BIT (Ds4L1);       }
  if (Report[6] & 0x02) { B |= DS4_BIT (Ds4R1);       }
  if (Report[6] & 0x04) { B |= DS4_BIT (Ds4L2);       }
  if (Report[6] & 0x08) { B |= DS4_BIT (Ds4R2);       }
  if (Report[6] & 0x10) { B |= DS4_BIT (Ds4Share);    }
  if (Report[6] & 0x20) { B |= DS4_BIT (Ds4Options);  }
  if (Report[6] & 0x40) { B |= DS4_BIT (Ds4L3);       }
  if (Report[6] & 0x80) { B |= DS4_BIT (Ds4R3);       }
  if (Report[7] & 0x01) { B |= DS4_BIT (Ds4Ps);       }
  if (Report[7] & 0x02) { B |= DS4_BIT (Ds4Touchpad); }

  *Buttons = B;
  return TRUE;
}

BOOLEAN
Ds4ButtonKey (
  IN  UINTN    Button,
  OUT DS4_KEY  *Key
  )
{
  if ((Button >= Ds4ButtonCount) || (Key == 0)) {
    return FALSE;
  }

  *Key = mKeys[Button];
  return (Key->ScanCode != SCAN_NULL) || (Key->UnicodeChar != CHAR_NULL);
}

BOOLEAN
Ds4ButtonRepeats (
  IN UINTN  Button
  )
{
  return (Button == Ds4Up) || (Button == Ds4Down) || (Button == Ds4Left) || (Button == Ds4Right) ||
         (Button == Ds4L1) || (Button == Ds4R1) || (Button == Ds4Triangle);
}

UINTN
Ds4NewKeys (
  IN  UINT32   Old,
  IN  UINT32   New,
  OUT DS4_KEY  *Keys,
  IN  UINTN    Max
  )
{
  UINT32  Pressed;
  UINTN   Button;
  UINTN   Count;

  Pressed = New & ~Old;
  Count   = 0;
  for (Button = 0; Button < Ds4ButtonCount && Count < Max; Button++) {
    if (((Pressed & DS4_BIT (Button)) != 0) && Ds4ButtonKey (Button, &Keys[Count])) {
      Count++;
    }
  }

  return Count;
}
