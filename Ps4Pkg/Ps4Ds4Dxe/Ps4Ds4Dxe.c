/** @file
  Ps4Ds4Dxe - a USB-cabled DualShock 4 as a UEFI keyboard (SimpleTextIn + SimpleTextInEx).

  Binds the HID interface of Sony 054c:05c4 (DS4 v1), 054c:09cc (DS4 v2) and 054c:0ba0 (wireless
  dongle), polls report 0x01 with an async interrupt transfer (no initialisation needed over USB,
  PS4 Linux hid-playstation.c:2401-2407) and turns button presses into keys (Ds4Report.c):
  D-pad = arrows, Cross = Enter, Circle = Esc, Square = Space, Triangle = Backspace, L1/R1 = PageUp/
  PageDown, Share = F1, Options = F2, PS = Home, touchpad click = Tab. Directions, page keys and
  Backspace repeat while held.

  The DS4's HID interface is not a boot keyboard, so the ConIn wildcard for USB keyboards does not
  match it; the entry point adds UsbClass(0x054C,0xFFFF,0x03,0xFF,0xFF) to ConIn so ConPlatform makes
  every Sony HID pad a console input.
**/

#include <Uefi.h>
#include <IndustryStandard/Usb.h>
#include <Guid/GlobalVariable.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/UsbIo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiUsbLib.h>
#include "Ds4Report.h"

#define CLASS_HID           3                                  // USB HID interface class
#define DS4_VENDOR          0x054C
#define DS4_PID_DONGLE      0x0BA0
#define DS4_SIGNATURE       SIGNATURE_32 ('D', 'S', '4', 'K')
#define DS4_NOTIFY_SIG      SIGNATURE_32 ('D', 'S', '4', 'N')
#define KEY_QUEUE           32
#define REPEAT_PERIOD       EFI_TIMER_PERIOD_MILLISECONDS (40)
#define REPEAT_DELAY_TICKS  12                                   // 480 ms before repeating
#define REPEAT_RATE_TICKS   2                                    // then every 80 ms
#define NO_BUTTON           ((UINTN)-1)

STATIC CONST UINT16  mPids[] = { 0x05C4, 0x09CC, DS4_PID_DONGLE };

typedef struct {
  UINTN                      Signature;
  LIST_ENTRY                 Link;
  EFI_KEY_DATA               KeyData;
  EFI_KEY_NOTIFY_FUNCTION    Function;
} DS4_NOTIFY;

typedef struct {
  DS4_KEY    Keys[KEY_QUEUE];
  UINTN      Head;
  UINTN      Tail;
} DS4_QUEUE;

typedef struct {
  UINTN                                Signature;
  EFI_HANDLE                           Controller;
  EFI_USB_IO_PROTOCOL                  *UsbIo;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL       In;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL    InEx;
  EFI_USB_ENDPOINT_DESCRIPTOR          Ep;
  BOOLEAN                              Dongle;
  UINT32                               Buttons;
  DS4_QUEUE                            Queue;            // for ReadKeyStroke(Ex)
  DS4_QUEUE                            NotifyQueue;      // for registered key notifications
  LIST_ENTRY                           NotifyList;
  EFI_EVENT                            RepeatTimer;
  EFI_EVENT                            RecoveryEvent;
  EFI_EVENT                            NotifyEvent;
  UINTN                                RepeatButton;
  UINTN                                HeldTicks;
} DS4_DEV;

#define DS4_FROM_IN(a)    CR (a, DS4_DEV, In, DS4_SIGNATURE)
#define DS4_FROM_INEX(a)  CR (a, DS4_DEV, InEx, DS4_SIGNATURE)

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDs4Binding;

//
// ---- key queues (callers run at TPL_NOTIFY or raise to it) ----
//
STATIC
BOOLEAN
QueuePut (
  IN DS4_QUEUE      *Q,
  IN CONST DS4_KEY  *Key
  )
{
  UINTN  Next;

  Next = (Q->Tail + 1) % KEY_QUEUE;
  if (Next == Q->Head) {
    return FALSE;                                            // full: drop
  }

  Q->Keys[Q->Tail] = *Key;
  Q->Tail          = Next;
  return TRUE;
}

STATIC
BOOLEAN
QueueGet (
  IN  DS4_QUEUE  *Q,
  OUT DS4_KEY    *Key
  )
{
  if (Q->Head == Q->Tail) {
    return FALSE;
  }

  *Key    = Q->Keys[Q->Head];
  Q->Head = (Q->Head + 1) % KEY_QUEUE;
  return TRUE;
}

STATIC
VOID
PushKey (
  IN DS4_DEV        *Dev,
  IN CONST DS4_KEY  *Key
  )
{
  LIST_ENTRY  *Link;
  DS4_NOTIFY  *N;

  QueuePut (&Dev->Queue, Key);
  for (Link = GetFirstNode (&Dev->NotifyList); !IsNull (&Dev->NotifyList, Link); Link = GetNextNode (&Dev->NotifyList, Link)) {
    N = CR (Link, DS4_NOTIFY, Link, DS4_NOTIFY_SIG);
    if ((N->KeyData.Key.ScanCode == Key->ScanCode) && (N->KeyData.Key.UnicodeChar == Key->UnicodeChar)) {
      QueuePut (&Dev->NotifyQueue, Key);
      gBS->SignalEvent (Dev->NotifyEvent);
      break;
    }
  }
}

//
// ---- USB ----
//
STATIC
EFI_STATUS
EFIAPI
Ds4ReportHandler (
  IN VOID    *Data,
  IN UINTN   DataLength,
  IN VOID    *Context,
  IN UINT32  Result
  );

STATIC
EFI_STATUS
SubmitPoll (
  IN DS4_DEV  *Dev
  )
{
  return Dev->UsbIo->UsbAsyncInterruptTransfer (
                       Dev->UsbIo,
                       Dev->Ep.EndpointAddress,
                       TRUE,
                       (Dev->Ep.Interval != 0) ? Dev->Ep.Interval : 4,
                       (Dev->Ep.MaxPacketSize >= DS4_REPORT_SIZE) ? DS4_REPORT_SIZE : Dev->Ep.MaxPacketSize,
                       Ds4ReportHandler,
                       Dev
                       );
}

STATIC
EFI_STATUS
EFIAPI
Ds4ReportHandler (
  IN VOID    *Data,
  IN UINTN   DataLength,
  IN VOID    *Context,
  IN UINT32  Result
  )
{
  DS4_DEV  *Dev;
  UINT32   New;
  UINT32   UsbStatus;
  DS4_KEY  Keys[Ds4ButtonCount];
  UINTN    Count;
  UINTN    Index;
  UINT32   Pressed;

  Dev = Context;
  if (Result != EFI_USB_NOERROR) {
    if ((Result & EFI_USB_ERR_STALL) == EFI_USB_ERR_STALL) {
      UsbClearEndpointHalt (Dev->UsbIo, Dev->Ep.EndpointAddress, &UsbStatus);
    }

    Dev->UsbIo->UsbAsyncInterruptTransfer (Dev->UsbIo, Dev->Ep.EndpointAddress, FALSE, 0, 0, NULL, NULL);
    Dev->RepeatButton = NO_BUTTON;
    gBS->SetTimer (Dev->RecoveryEvent, TimerRelative, EFI_USB_INTERRUPT_DELAY);
    return EFI_DEVICE_ERROR;
  }

  if ((Data == NULL) || !Ds4ParseReport (Data, DataLength, Dev->Dongle, &New)) {
    return EFI_SUCCESS;
  }

  Count = Ds4NewKeys (Dev->Buttons, New, Keys, ARRAY_SIZE (Keys));
  for (Index = 0; Index < Count; Index++) {
    PushKey (Dev, &Keys[Index]);
  }

  //
  // Auto-repeat follows the most recently pressed repeating button while it stays held.
  //
  Pressed = New & ~Dev->Buttons;
  for (Index = 0; Index < Ds4ButtonCount; Index++) {
    if (((Pressed & DS4_BIT (Index)) != 0) && Ds4ButtonRepeats (Index)) {
      Dev->RepeatButton = Index;
      Dev->HeldTicks    = 0;
    }
  }

  if ((Dev->RepeatButton != NO_BUTTON) && ((New & DS4_BIT (Dev->RepeatButton)) == 0)) {
    Dev->RepeatButton = NO_BUTTON;
  }

  Dev->Buttons = New;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
Ds4RecoveryHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  SubmitPoll (Context);
}

STATIC
VOID
EFIAPI
Ds4RepeatHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DS4_DEV  *Dev;
  DS4_KEY  Key;

  Dev = Context;
  if ((Dev->RepeatButton == NO_BUTTON) || ((Dev->Buttons & DS4_BIT (Dev->RepeatButton)) == 0)) {
    Dev->RepeatButton = NO_BUTTON;
    return;
  }

  Dev->HeldTicks++;
  if ((Dev->HeldTicks >= REPEAT_DELAY_TICKS) &&
      (((Dev->HeldTicks - REPEAT_DELAY_TICKS) % REPEAT_RATE_TICKS) == 0) &&
      Ds4ButtonKey (Dev->RepeatButton, &Key))
  {
    PushKey (Dev, &Key);
  }
}

STATIC
VOID
EFIAPI
Ds4NotifyHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DS4_DEV       *Dev;
  DS4_KEY       Key;
  EFI_TPL       OldTpl;
  BOOLEAN       Have;
  LIST_ENTRY    *Link;
  DS4_NOTIFY    *N;
  EFI_KEY_DATA  KeyData;

  Dev = Context;
  for ( ; ; ) {
    OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    Have   = QueueGet (&Dev->NotifyQueue, &Key);
    gBS->RestoreTPL (OldTpl);
    if (!Have) {
      return;
    }

    ZeroMem (&KeyData, sizeof (KeyData));
    KeyData.Key.ScanCode            = Key.ScanCode;
    KeyData.Key.UnicodeChar         = Key.UnicodeChar;
    KeyData.KeyState.KeyShiftState  = EFI_SHIFT_STATE_VALID;
    KeyData.KeyState.KeyToggleState = EFI_TOGGLE_STATE_VALID;
    for (Link = GetFirstNode (&Dev->NotifyList); !IsNull (&Dev->NotifyList, Link); Link = GetNextNode (&Dev->NotifyList, Link)) {
      N = CR (Link, DS4_NOTIFY, Link, DS4_NOTIFY_SIG);
      if ((N->KeyData.Key.ScanCode == Key.ScanCode) && (N->KeyData.Key.UnicodeChar == Key.UnicodeChar)) {
        N->Function (&KeyData);
      }
    }
  }
}

//
// ---- SimpleTextIn / SimpleTextInEx ----
//
STATIC
EFI_STATUS
Dequeue (
  IN  DS4_DEV       *Dev,
  OUT EFI_KEY_DATA  *KeyData
  )
{
  EFI_TPL  OldTpl;
  DS4_KEY  Key;
  BOOLEAN  Have;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  Have   = QueueGet (&Dev->Queue, &Key);
  gBS->RestoreTPL (OldTpl);
  if (!Have) {
    return EFI_NOT_READY;
  }

  ZeroMem (KeyData, sizeof (*KeyData));
  KeyData->Key.ScanCode            = Key.ScanCode;
  KeyData->Key.UnicodeChar         = Key.UnicodeChar;
  KeyData->KeyState.KeyShiftState  = EFI_SHIFT_STATE_VALID;
  KeyData->KeyState.KeyToggleState = EFI_TOGGLE_STATE_VALID;
  return EFI_SUCCESS;
}

STATIC
VOID
Flush (
  IN DS4_DEV  *Dev
  )
{
  EFI_TPL  OldTpl;

  OldTpl          = gBS->RaiseTPL (TPL_NOTIFY);
  Dev->Queue.Head = Dev->Queue.Tail = 0;
  gBS->RestoreTPL (OldTpl);
}

STATIC
EFI_STATUS
EFIAPI
Ds4Reset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  Flush (DS4_FROM_IN (This));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ds4ReadKeyStroke (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  EFI_KEY_DATA  KeyData;
  EFI_STATUS    Status;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Dequeue (DS4_FROM_IN (This), &KeyData);
  if (!EFI_ERROR (Status)) {
    *Key = KeyData.Key;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Ds4ResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  Flush (DS4_FROM_INEX (This));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ds4ReadKeyStrokeEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  return Dequeue (DS4_FROM_INEX (This), KeyData);
}

STATIC
EFI_STATUS
EFIAPI
Ds4SetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return (KeyToggleState == NULL) ? EFI_INVALID_PARAMETER : EFI_SUCCESS;   // a pad has no lock keys
}

STATIC
EFI_STATUS
EFIAPI
Ds4RegisterKeyNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  DS4_DEV     *Dev;
  DS4_NOTIFY  *N;
  EFI_TPL     OldTpl;

  if ((KeyData == NULL) || (KeyNotificationFunction == NULL) || (NotifyHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  N = AllocateZeroPool (sizeof (*N));
  if (N == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev          = DS4_FROM_INEX (This);
  N->Signature = DS4_NOTIFY_SIG;
  N->Function  = KeyNotificationFunction;
  CopyMem (&N->KeyData, KeyData, sizeof (*KeyData));
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  InsertTailList (&Dev->NotifyList, &N->Link);
  gBS->RestoreTPL (OldTpl);
  *NotifyHandle = N;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ds4UnregisterKeyNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  DS4_DEV     *Dev;
  LIST_ENTRY  *Link;
  EFI_TPL     OldTpl;

  if (NotificationHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev    = DS4_FROM_INEX (This);
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (Link = GetFirstNode (&Dev->NotifyList); !IsNull (&Dev->NotifyList, Link); Link = GetNextNode (&Dev->NotifyList, Link)) {
    if (Link == &((DS4_NOTIFY *)NotificationHandle)->Link) {
      RemoveEntryList (Link);
      gBS->RestoreTPL (OldTpl);
      FreePool (NotificationHandle);
      return EFI_SUCCESS;
    }
  }

  gBS->RestoreTPL (OldTpl);
  return EFI_INVALID_PARAMETER;
}

STATIC
VOID
EFIAPI
Ds4WaitForKey (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DS4_DEV  *Dev;

  Dev = Context;
  if (Dev->Queue.Head != Dev->Queue.Tail) {
    gBS->SignalEvent (Event);
  }
}

//
// ---- driver binding ----
//
STATIC
BOOLEAN
IsDs4Interface (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo,
  OUT UINT16               *Pid OPTIONAL
  )
{
  EFI_USB_DEVICE_DESCRIPTOR     Dev;
  EFI_USB_INTERFACE_DESCRIPTOR  If;
  UINTN                         Index;

  if (EFI_ERROR (UsbIo->UsbGetDeviceDescriptor (UsbIo, &Dev)) || (Dev.IdVendor != DS4_VENDOR)) {
    return FALSE;
  }

  if (EFI_ERROR (UsbIo->UsbGetInterfaceDescriptor (UsbIo, &If)) || (If.InterfaceClass != CLASS_HID)) {
    return FALSE;
  }

  for (Index = 0; Index < ARRAY_SIZE (mPids); Index++) {
    if (Dev.IdProduct == mPids[Index]) {
      if (Pid != NULL) {
        *Pid = Dev.IdProduct;
      }

      return TRUE;
    }
  }

  return FALSE;
}

STATIC
EFI_STATUS
EFIAPI
Ds4Supported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  EFI_USB_IO_PROTOCOL  *UsbIo;
  EFI_STATUS           Status;

  Status = gBS->OpenProtocol (Controller, &gEfiUsbIoProtocolGuid, (VOID **)&UsbIo, This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = IsDs4Interface (UsbIo, NULL) ? EFI_SUCCESS : EFI_UNSUPPORTED;
  gBS->CloseProtocol (Controller, &gEfiUsbIoProtocolGuid, This->DriverBindingHandle, Controller);
  return Status;
}

STATIC
VOID
FreeDev (
  IN DS4_DEV  *Dev
  )
{
  LIST_ENTRY  *Link;

  if (Dev->RepeatTimer != NULL) {
    gBS->CloseEvent (Dev->RepeatTimer);
  }

  if (Dev->RecoveryEvent != NULL) {
    gBS->CloseEvent (Dev->RecoveryEvent);
  }

  if (Dev->NotifyEvent != NULL) {
    gBS->CloseEvent (Dev->NotifyEvent);
  }

  if (Dev->In.WaitForKey != NULL) {
    gBS->CloseEvent (Dev->In.WaitForKey);
  }

  if (Dev->InEx.WaitForKeyEx != NULL) {
    gBS->CloseEvent (Dev->InEx.WaitForKeyEx);
  }

  while (!IsListEmpty (&Dev->NotifyList)) {
    Link = GetFirstNode (&Dev->NotifyList);
    RemoveEntryList (Link);
    FreePool (CR (Link, DS4_NOTIFY, Link, DS4_NOTIFY_SIG));
  }

  FreePool (Dev);
}

STATIC
EFI_STATUS
EFIAPI
Ds4Start (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  DS4_DEV                       *Dev;
  EFI_STATUS                    Status;
  EFI_USB_INTERFACE_DESCRIPTOR  If;
  EFI_USB_ENDPOINT_DESCRIPTOR   Ep;
  UINT16                        Pid;
  UINT8                         Index;
  BOOLEAN                       Found;

  Dev = AllocateZeroPool (sizeof (*Dev));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature    = DS4_SIGNATURE;
  Dev->Controller   = Controller;
  Dev->RepeatButton = NO_BUTTON;
  InitializeListHead (&Dev->NotifyList);

  Status = gBS->OpenProtocol (Controller, &gEfiUsbIoProtocolGuid, (VOID **)&Dev->UsbIo, This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    FreePool (Dev);
    return Status;
  }

  Found = FALSE;
  if (IsDs4Interface (Dev->UsbIo, &Pid) && !EFI_ERROR (Dev->UsbIo->UsbGetInterfaceDescriptor (Dev->UsbIo, &If))) {
    for (Index = 0; Index < If.NumEndpoints && !Found; Index++) {
      if (!EFI_ERROR (Dev->UsbIo->UsbGetEndpointDescriptor (Dev->UsbIo, Index, &Ep)) &&
          ((Ep.Attributes & (BIT0 | BIT1)) == USB_ENDPOINT_INTERRUPT) &&
          ((Ep.EndpointAddress & USB_ENDPOINT_DIR_IN) != 0))
      {
        CopyMem (&Dev->Ep, &Ep, sizeof (Ep));
        Found = TRUE;
      }
    }
  }

  if (!Found) {
    DEBUG ((DEBUG_ERROR, "Ps4Ds4: no interrupt IN endpoint on the HID interface\n"));
    Status = EFI_UNSUPPORTED;
    goto Fail;
  }

  Dev->Dongle                     = (Pid == DS4_PID_DONGLE);
  Dev->In.Reset                   = Ds4Reset;
  Dev->In.ReadKeyStroke           = Ds4ReadKeyStroke;
  Dev->InEx.Reset                 = Ds4ResetEx;
  Dev->InEx.ReadKeyStrokeEx       = Ds4ReadKeyStrokeEx;
  Dev->InEx.SetState              = Ds4SetState;
  Dev->InEx.RegisterKeyNotify     = Ds4RegisterKeyNotify;
  Dev->InEx.UnregisterKeyNotify   = Ds4UnregisterKeyNotify;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, Ds4WaitForKey, Dev, &Dev->In.WaitForKey);
  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, Ds4WaitForKey, Dev, &Dev->InEx.WaitForKeyEx);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Ds4RepeatHandler, Dev, &Dev->RepeatTimer);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Ds4RecoveryHandler, Dev, &Dev->RecoveryEvent);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Ds4NotifyHandler, Dev, &Dev->NotifyEvent);
  }

  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Controller,
                  &gEfiSimpleTextInProtocolGuid,
                  &Dev->In,
                  &gEfiSimpleTextInputExProtocolGuid,
                  &Dev->InEx,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    goto Fail;
  }

  Status = SubmitPoll (Dev);
  if (EFI_ERROR (Status)) {
    gBS->UninstallMultipleProtocolInterfaces (Controller, &gEfiSimpleTextInProtocolGuid, &Dev->In, &gEfiSimpleTextInputExProtocolGuid, &Dev->InEx, NULL);
    goto Fail;
  }

  gBS->SetTimer (Dev->RepeatTimer, TimerPeriodic, REPEAT_PERIOD);
  DEBUG ((DEBUG_INFO, "Ps4Ds4: DualShock 4 054c:%04x ep 0x%02x interval %u maxpkt %u: started\n", Pid, Dev->Ep.EndpointAddress, Dev->Ep.Interval, Dev->Ep.MaxPacketSize));
  return EFI_SUCCESS;

Fail:
  DEBUG ((DEBUG_ERROR, "Ps4Ds4: start failed: %r\n", Status));
  gBS->CloseProtocol (Controller, &gEfiUsbIoProtocolGuid, This->DriverBindingHandle, Controller);
  FreeDev (Dev);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Ds4Stop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer
  )
{
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *In;
  DS4_DEV                         *Dev;
  EFI_STATUS                      Status;

  Status = gBS->OpenProtocol (Controller, &gEfiSimpleTextInProtocolGuid, (VOID **)&In, This->DriverBindingHandle, Controller, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  Dev = DS4_FROM_IN (In);
  gBS->SetTimer (Dev->RepeatTimer, TimerCancel, 0);
  gBS->SetTimer (Dev->RecoveryEvent, TimerCancel, 0);
  Dev->UsbIo->UsbAsyncInterruptTransfer (Dev->UsbIo, Dev->Ep.EndpointAddress, FALSE, 0, 0, NULL, NULL);
  Status = gBS->UninstallMultipleProtocolInterfaces (Controller, &gEfiSimpleTextInProtocolGuid, &Dev->In, &gEfiSimpleTextInputExProtocolGuid, &Dev->InEx, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (Controller, &gEfiUsbIoProtocolGuid, This->DriverBindingHandle, Controller);
  FreeDev (Dev);
  return EFI_SUCCESS;
}

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDs4Binding = {
  Ds4Supported,
  Ds4Start,
  Ds4Stop,
  0x10,
  NULL,
  NULL
};

/* ConIn += UsbClass(0x054C, 0xFFFF, HID, any, any), unless already present. */
STATIC
VOID
AddPadsToConIn (
  VOID
  )
{
  struct {
    USB_CLASS_DEVICE_PATH       Class;
    EFI_DEVICE_PATH_PROTOCOL    End;
  }                         Path;
  EFI_DEVICE_PATH_PROTOCOL  *Old;
  EFI_DEVICE_PATH_PROTOCOL  *New;
  UINTN                     OldSize;
  UINTN                     Offset;
  EFI_STATUS                Status;

  ZeroMem (&Path, sizeof (Path));
  Path.Class.Header.Type    = MESSAGING_DEVICE_PATH;
  Path.Class.Header.SubType = MSG_USB_CLASS_DP;
  SetDevicePathNodeLength (&Path.Class.Header, sizeof (Path.Class));
  Path.Class.VendorId       = DS4_VENDOR;
  Path.Class.ProductId      = 0xFFFF;
  Path.Class.DeviceClass    = CLASS_HID;
  Path.Class.DeviceSubClass = 0xFF;
  Path.Class.DeviceProtocol = 0xFF;
  SetDevicePathEndNode (&Path.End);

  Old     = NULL;
  OldSize = 0;
  if (!EFI_ERROR (GetVariable2 (L"ConIn", &gEfiGlobalVariableGuid, (VOID **)&Old, &OldSize)) && (Old != NULL)) {
    for (Offset = 0; Offset + sizeof (Path.Class) <= OldSize; Offset++) {
      if (CompareMem ((UINT8 *)Old + Offset, &Path.Class, sizeof (Path.Class)) == 0) {
        FreePool (Old);
        return;
      }
    }
  } else {
    Old = NULL;
  }

  New = AppendDevicePathInstance (Old, (EFI_DEVICE_PATH_PROTOCOL *)&Path);
  if (Old != NULL) {
    FreePool (Old);
  }

  if (New == NULL) {
    return;
  }

  Status = gRT->SetVariable (
                  L"ConIn",
                  &gEfiGlobalVariableGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                  GetDevicePathSize (New),
                  New
                  );
  DEBUG ((DEBUG_INFO, "Ps4Ds4: ConIn += UsbClass(0x054C,0xFFFF,0x03,0xFF,0xFF): %r\n", Status));
  FreePool (New);
}

EFI_STATUS
EFIAPI
Ps4Ds4EntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = EfiLibInstallDriverBinding (ImageHandle, SystemTable, &mDs4Binding, ImageHandle);
  if (!EFI_ERROR (Status)) {
    AddPadsToConIn ();
  }

  return Status;
}
