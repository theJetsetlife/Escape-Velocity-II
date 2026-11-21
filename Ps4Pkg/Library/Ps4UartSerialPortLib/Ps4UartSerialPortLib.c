/** @file
  Ps4UartSerialPortLib - SerialPortLib on the southbridge UART only (no framebuffer drawing).

  Used by SerialDxe so TerminalDxe gets a serial ConIn/ConOut (Shell over the UART) while the
  screen keeps GraphicsConsole; DEBUG output keeps going through Ps4FbSerialPortLib, which mirrors
  to the same UART.
**/

#include <Base.h>
#include <Library/SerialPortLib.h>          // brings UefiBaseType.h + Protocol/SerialIo.h
#include <Library/Ps4UartLib.h>

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return Ps4UartPresent () ? RETURN_SUCCESS : RETURN_DEVICE_ERROR;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  if (Buffer == NULL) {
    return 0;
  }

  Ps4UartWrite (Buffer, NumberOfBytes);
  return NumberOfBytes;                                   // a dead UART must not stall callers
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return Ps4UartRead (Buffer, NumberOfBytes);
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return Ps4UartPoll ();
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  *Control = 0;
  if (!Ps4UartPoll ()) {
    *Control |= EFI_SERIAL_INPUT_BUFFER_EMPTY;
  }

  if (Ps4UartTxEmpty ()) {
    *Control |= EFI_SERIAL_OUTPUT_BUFFER_EMPTY;
  }

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
  //
  // The line stays at 115200 8N1 (Ps4UartLib); report that back whatever was asked, so SerialDxe /
  // TerminalDxe never fail on a mismatching default.
  //
  *BaudRate = 115200;
  *DataBits = 8;
  *Parity   = NoParity;
  *StopBits = OneStopBit;
  return RETURN_SUCCESS;
}
