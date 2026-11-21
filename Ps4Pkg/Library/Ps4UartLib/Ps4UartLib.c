/** @file
  Ps4UartLib - see Include/Library/Ps4UartLib.h.

  Discovery reads PCI config only (ECAM from MSR C001_0058 on family 16h, the PS4 CPU); it never
  probes I/O ports on a family-16h machine. The line is reprogrammed only when it is not already
  115200 8N1, so modules that each discover the UART do not disturb bytes in flight.
**/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/Ps4UartLib.h>

#define RBR  0
#define THR  0
#define DLL  0
#define IER  1
#define DLM  1
#define FCR  2
#define LCR  3
#define MCR  4
#define LSR  5
#define SCR  7

#define LCR_8N1   0x03
#define LCR_DLAB  0x80
#define LSR_DR    BIT0
#define LSR_THRE  BIT5

#define BAUD           115200
#define TX_WAIT_POLLS  20000          // ~ one 16-byte FIFO at 115200 plus margin
#define TX_MAX_STRIKES 8              // consecutive timeouts before the UART is given up
#define SONY_VENDOR    0x104D

typedef enum {
  UartUnknown = 0,
  UartNone,
  UartMmio32,
  UartIo
} UART_KIND;

STATIC UART_KIND  mKind = UartUnknown;
STATIC UINTN      mBase;
STATIC UINT32     mClock;
STATIC UINTN      mStrikes;

STATIC
UINT8
Rd (
  IN UINTN  Reg
  )
{
  return (mKind == UartMmio32) ? (UINT8)MmioRead32 (mBase + Reg * 4) : IoRead8 (mBase + Reg);
}

STATIC
VOID
Wr (
  IN UINTN  Reg,
  IN UINT8  Value
  )
{
  if (mKind == UartMmio32) {
    MmioWrite32 (mBase + Reg * 4, Value);
  } else {
    IoWrite8 (mBase + Reg, Value);
  }
}

STATIC
UINT64
EcamBar (
  IN UINTN  Fn,
  IN UINTN  Bar,
  IN UINTN  Ecam
  )
{
  UINTN   Cfg;
  UINT32  Lo;
  UINT32  Hi;

  Cfg = Ecam + (0x14 << 15) + (Fn << 12);
  Lo  = MmioRead32 (Cfg + 0x10 + Bar * 4);
  if ((Lo & BIT0) != 0) {
    return 0;
  }

  Hi = (((Lo >> 1) & 3) == 2 && Bar < 5) ? MmioRead32 (Cfg + 0x10 + (Bar + 1) * 4) : 0;
  return LShiftU64 (Hi, 32) | (Lo & ~(UINT32)0xF);
}

STATIC
BOOLEAN
IsFamily16h (
  VOID
  )
{
  UINT32  Ebx;
  UINT32  Ecx;
  UINT32  Edx;
  UINT32  Eax;
  UINT32  Family;

  AsmCpuid (0, NULL, &Ebx, &Ecx, &Edx);
  if ((Ebx != 0x68747541) || (Edx != 0x69746E65) || (Ecx != 0x444D4163)) {   // "AuthenticAMD"
    return FALSE;
  }

  AsmCpuid (1, &Eax, NULL, NULL, NULL);
  Family = (Eax >> 8) & 0xF;
  if (Family == 0xF) {
    Family += (Eax >> 20) & 0xFF;
  }

  return Family == 0x16;
}

STATIC
VOID
Discover (
  VOID
  )
{
  UINT64  Msr;
  UINTN   Ecam;
  UINT32  Id;
  UINT64  Bar;

  mKind = UartNone;
  if (IsFamily16h ()) {
    Msr = AsmReadMsr64 (0xC0010058);                        // MMIO_CFG_BASE (PS4: 0xf8000019)
    if ((Msr & BIT0) == 0) {
      return;
    }

    Ecam = (UINTN)(Msr & 0x0000FFFFFFF00000ULL);
    Id   = MmioRead32 (Ecam + (0x14 << 15) + (4 << 12));
    if ((Id == (0x90a1u << 16 | SONY_VENDOR)) || (Id == (0x90ccu << 16 | SONY_VENDOR))) {
      Bar    = EcamBar (4, 4, Ecam);
      mBase  = (UINTN)Bar + 0x140000;
      mClock = 58500000;
    } else if (Id == (0x90dbu << 16 | SONY_VENDOR)) {
      Bar    = EcamBar (4, 2, Ecam);
      mBase  = (UINTN)Bar + 0x10E000;
      mClock = 25000000;
    } else {
      return;
    }

    if ((Bar == 0) || (Bar >= BASE_4GB)) {
      return;
    }

    mKind = UartMmio32;
  } else {
    IoWrite8 (0x3F8 + SCR, 0x5A);                           // COM1 present? (QEMU harness)
    if (IoRead8 (0x3F8 + SCR) != 0x5A) {
      return;
    }

    mBase  = 0x3F8;
    mClock = 1843200;
    mKind  = UartIo;
  }

  //
  // 115200 8N1, interrupts off. Reprogram only when the line is set differently.
  //
  {
    UINT16  Want;
    UINT8   Lcr;
    UINT16  Have;

    Want = (UINT16)((mClock + 8 * BAUD) / (16 * BAUD));
    Lcr  = Rd (LCR);
    Wr (LCR, (UINT8)(Lcr | LCR_DLAB));
    Have = (UINT16)(Rd (DLL) | (Rd (DLM) << 8));
    if ((Have != Want) || ((Lcr & 0x7F) != LCR_8N1)) {
      Wr (DLL, (UINT8)Want);
      Wr (DLM, (UINT8)(Want >> 8));
      Wr (LCR, LCR_8N1);
      Wr (FCR, 0x07);                                       // FIFOs on, cleared
      Wr (MCR, 0x03);                                       // DTR, RTS
    } else {
      Wr (LCR, LCR_8N1);
    }

    Wr (IER, 0);                                            // polled only, never interrupts
  }
}

BOOLEAN
EFIAPI
Ps4UartPresent (
  VOID
  )
{
  if (mKind == UartUnknown) {
    Discover ();
  }

  return mKind == UartMmio32 || mKind == UartIo;
}

BOOLEAN
EFIAPI
Ps4UartTxEmpty (
  VOID
  )
{
  return Ps4UartPresent () && ((Rd (LSR) & LSR_THRE) != 0);
}

UINTN
EFIAPI
Ps4UartWrite (
  IN CONST UINT8  *Buffer,
  IN UINTN        Count
  )
{
  UINTN  Index;
  UINTN  Polls;

  if (!Ps4UartPresent () || (Buffer == NULL)) {
    return 0;
  }

  for (Index = 0; Index < Count; Index++) {
    for (Polls = 0; (Rd (LSR) & LSR_THRE) == 0 && Polls < TX_WAIT_POLLS; Polls++) {
    }

    if (Polls >= TX_WAIT_POLLS) {                         // byte dropped; a dead line is given up
      if (++mStrikes >= TX_MAX_STRIKES) {
        mKind = UartNone;
        return Index;
      }

      continue;
    }

    mStrikes = 0;
    Wr (THR, Buffer[Index]);
  }

  return Count;
}

BOOLEAN
EFIAPI
Ps4UartPoll (
  VOID
  )
{
  return Ps4UartPresent () && ((Rd (LSR) & LSR_DR) != 0);
}

UINTN
EFIAPI
Ps4UartRead (
  OUT UINT8  *Buffer,
  IN  UINTN  Count
  )
{
  UINTN  Index;

  if (!Ps4UartPresent () || (Buffer == NULL)) {
    return 0;
  }

  for (Index = 0; Index < Count && (Rd (LSR) & LSR_DR) != 0; Index++) {
    Buffer[Index] = Rd (RBR);
  }

  return Index;
}
