/** @file
  SerialPortLib that draws text on the framebuffer the directboot stage left the display on.

  The PS4 has no legacy UART; this makes every DEBUG()/ASSERT line of every EDK II module
  visible from the first instruction of the UPL entry. Cursor state lives in the shared
  descriptor (Ps4FbConsole.h) so all modules continue the same screen.

  Page mode (2026-11-20): when the screen is full the whole text area is cleared and output
  restarts at the top (no 8 MiB memmove through an uncached framebuffer, and no stale lines
  below the newest one: photos used to mix two passes). Every line starts with
  [seconds.milliseconds] since the stage took over, and a green cursor block is drawn after
  each write: no cursor on a frozen screen = the CPU stopped inside a print.
  Font: upstream MdeModulePkg GraphicsConsoleDxe LaffStd.c glyphs, converted by mkplatform.py.
**/

#include <Base.h>
#include <Library/SerialPortLib.h>
#include <Library/Ps4UartLib.h>
#include <Ps4FbConsole.h>
#include "Font8x19.h"

#define CELL_W  8
#define CELL_H  19
#define FG      0xFFE0E0E0u
#define BG      0xFF000000u
#define CURSOR  0xFF00C000u

STATIC
PS4_FBCON *
Con (
  VOID
  )
{
  PS4_FBCON  *C;

  C = (PS4_FBCON *)(UINTN)PS4_FBCON_ADDRESS;
  if ((C->Magic != PS4_FBCON_MAGIC) || (C->FbBase == 0) || (C->Width < CELL_W) || (C->Height < CELL_H)) {
    return NULL;
  }

  return C;
}

STATIC
VOID
ClearRow (
  IN PS4_FBCON  *C,
  IN UINT32     Row
  )
{
  UINT32  Y;
  UINT32  X;

  for (Y = Row * CELL_H; Y < (Row + 1) * CELL_H && Y < C->Height; Y++) {
    volatile UINT32  *Line = (volatile UINT32 *)(UINTN)(C->FbBase + (UINT64)Y * C->PitchBytes);
    for (X = 0; X < C->Width; X++) {
      Line[X] = BG;
    }
  }
}

STATIC
VOID
ClearText (
  IN PS4_FBCON  *C
  )
{
  UINT32  Row;

  for (Row = PS4_FBCON_FIRST_ROW; Row < C->Height / CELL_H; Row++) {
    ClearRow (C, Row);
  }
}

STATIC
VOID
NewLine (
  IN PS4_FBCON  *C
  )
{
  C->Col = 0;
  C->Row++;
  if (C->Row >= C->Height / CELL_H) {
    C->Row = PS4_FBCON_FIRST_ROW;
    ClearText (C);                          // page mode: never leave older lines below the newest
  } else {
    ClearRow (C, C->Row);
  }
}

STATIC
VOID
FillCell (
  IN PS4_FBCON  *C,
  IN UINT32     Colour
  )
{
  UINT32  Y;
  UINT32  X;

  if ((C->Row + 1) * CELL_H > C->Height || (C->Col + 1) * CELL_W > C->Width) {
    return;
  }

  for (Y = 0; Y < CELL_H; Y++) {
    volatile UINT32  *Line = (volatile UINT32 *)(UINTN)(C->FbBase + (UINT64)(C->Row * CELL_H + Y) * C->PitchBytes) + C->Col * CELL_W;
    for (X = 0; X < CELL_W; X++) {
      Line[X] = Colour;
    }
  }
}

STATIC
VOID
PutGlyph (
  IN PS4_FBCON  *C,
  IN UINT8      Ch
  );

STATIC
VOID
PutStamp (
  IN PS4_FBCON  *C
  )
{
  UINT64  Ms;
  UINT8   Buf[16];
  UINT32  N;
  UINT32  I;
  UINT64  Sec;

  if ((C->Version < 2) || (C->TscHz < 1000)) {
    return;
  }

  Ms  = (__builtin_ia32_rdtsc () - C->Tsc0) / (C->TscHz / 1000);
  Sec = Ms / 1000;
  N   = 0;
  Buf[N++] = '[';
  {
    UINT8   D[20];
    UINT32  K = 0;
    do {
      D[K++] = (UINT8)('0' + Sec % 10);
      Sec   /= 10;
    } while (Sec && K < sizeof (D));

    for (I = K; I < 4; I++) {
      Buf[N++] = ' ';
    }

    while (K && N < sizeof (Buf) - 6) {
      Buf[N++] = D[--K];
    }
  }
  Buf[N++] = '.';
  Buf[N++] = (UINT8)('0' + (Ms / 100) % 10);
  Buf[N++] = (UINT8)('0' + (Ms / 10) % 10);
  Buf[N++] = (UINT8)('0' + Ms % 10);
  Buf[N++] = ']';
  Buf[N++] = ' ';
  for (I = 0; I < N; I++) {
    PutGlyph (C, Buf[I]);
  }
}

STATIC
VOID
PutGlyph (
  IN PS4_FBCON  *C,
  IN UINT8      Ch
  )
{
  CONST UINT8  *G;
  UINT32       Y;
  UINT32       X;

  if ((Ch < 0x20) || (Ch > 0x7E)) {
    Ch = '?';
  }

  G = mPs4Font8x19[Ch - 0x20];
  for (Y = 0; Y < CELL_H; Y++) {
    volatile UINT32  *Line = (volatile UINT32 *)(UINTN)(C->FbBase + (UINT64)(C->Row * CELL_H + Y) * C->PitchBytes) + C->Col * CELL_W;
    for (X = 0; X < CELL_W; X++) {
      Line[X] = (G[Y] & (0x80 >> X)) ? FG : BG;
    }
  }

  if (++C->Col >= C->Width / CELL_W) {
    NewLine (C);
  }
}

/* every byte also goes into the boot-log ring the stage published (descriptor v3), so
   ps4log can save the whole boot to a file. Silently skipped when the ring is absent/invalid. */
STATIC
VOID
LogAppend (
  IN CONST UINT8  *Buffer,
  IN UINTN        Count
  )
{
  PS4_FBCON       *C;
  PS4_LOG_HEADER  *H;
  UINT8           *D;
  UINTN           I;

  C = (PS4_FBCON *)(UINTN)PS4_FBCON_ADDRESS;
  if ((C->Magic != PS4_FBCON_MAGIC) || (C->Version < 3) || (C->LogBase == 0) || (C->LogSize == 0)) {
    return;
  }

  H = (PS4_LOG_HEADER *)(UINTN)C->LogBase;
  if ((H->Magic != PS4_LOG_MAGIC) || (H->Size != C->LogSize)) {
    return;
  }

  D = (UINT8 *)(H + 1);
  for (I = 0; I < Count; I++) {
    D[H->Written++ % H->Size] = Buffer[I];
  }
}

/* Every DEBUG line also goes to the southbridge UART (Ps4UartLib), "\n" sent as CR LF. */
STATIC
VOID
UartMirror (
  IN CONST UINT8  *Buffer,
  IN UINTN        Count
  )
{
  STATIC CONST UINT8  Cr = '\r';
  UINTN               Start;
  UINTN               I;

  if (!Ps4UartPresent ()) {
    return;
  }

  Start = 0;
  for (I = 0; I < Count; I++) {
    if ((Buffer[I] == '\n') && ((I == 0) || (Buffer[I - 1] != '\r'))) {
      Ps4UartWrite (Buffer + Start, I - Start);
      Ps4UartWrite (&Cr, 1);
      Start = I;
    }
  }

  Ps4UartWrite (Buffer + Start, Count - Start);
}

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  PS4_FBCON  *C;
  UINTN      I;

  if (Buffer == NULL) {
    return NumberOfBytes;
  }

  LogAppend (Buffer, NumberOfBytes);
  C = Con ();
  if (C == NULL) {
    UartMirror (Buffer, NumberOfBytes);
    return NumberOfBytes;
  }

  if ((C->Reserved & PS4_FBCON_FLAG_DEBUGCON) != 0) {   // QEMU harness only (see Ps4FbConsole.h)
    for (I = 0; I < NumberOfBytes; I++) {
      __asm__ __volatile__ ("outb %0, $0xE9" : : "a" (Buffer[I]));
    }
  }

  FillCell (C, BG);                         // erase the previous cursor
  for (I = 0; I < NumberOfBytes; I++) {
    if (Buffer[I] == '\n') {
      NewLine (C);
    } else if (Buffer[I] == '\r') {
      C->Col = 0;
    } else {
      if (C->Col == 0) {
        PutStamp (C);
      }

      PutGlyph (C, Buffer[I]);
    }
  }

  FillCell (C, CURSOR);                     // drawn only once the whole write finished
  UartMirror (Buffer, NumberOfBytes);       // after the screen: a stuck UART cannot hide a line
  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  *Control = 0;
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_SUCCESS;
}
