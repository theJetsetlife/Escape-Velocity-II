/** @file
  Ps4UartLib - polled 16550 access to the PS4 southbridge UART (or COM1 off the PS4, for QEMU).

  PS4 (CPU family 16h with a Sony southbridge at 00:14.4): Aeolia 104d:90a1 / Belize 90cc UART0 at
  BAR4 + 0x140000 (0xd0340000 on the Fat), Baikal 90db at BAR2 + 0x10E000; 8250-compatible, 32-bit
  registers 4 bytes apart, clock 58.5 MHz (Baikal 25 MHz) (PS4 Linux sb-uart.c, loader uart.c).
  Other machines: COM1 at I/O 0x3F8 when its scratch register answers. 115200 8N1, interrupts off.
  Every wait is bounded; a UART that stops draining is dropped for the rest of the module.
**/

#ifndef PS4_UART_LIB_H_
#define PS4_UART_LIB_H_

#include <Base.h>

/** TRUE when a UART was found and is set to 115200 8N1 (discovered once per module). */
BOOLEAN
EFIAPI
Ps4UartPresent (
  VOID
  );

/** Send Count bytes as they are. Returns the number sent. */
UINTN
EFIAPI
Ps4UartWrite (
  IN CONST UINT8  *Buffer,
  IN UINTN        Count
  );

/** TRUE when at least one received byte is waiting. */
BOOLEAN
EFIAPI
Ps4UartPoll (
  VOID
  );

/** Read up to Count waiting bytes without blocking. Returns the number read. */
UINTN
EFIAPI
Ps4UartRead (
  OUT UINT8  *Buffer,
  IN  UINTN  Count
  );

/** TRUE when the transmitter holding register is empty. */
BOOLEAN
EFIAPI
Ps4UartTxEmpty (
  VOID
  );

#endif
