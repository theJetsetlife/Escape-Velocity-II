/** @file
  Ps4ApicFixupDxe - clear the local APIC state the PS4 takeover leaves behind.

  The directboot hook (icc_query_nowait) can run inside an OrbisOS interrupt handler. That vector
  is then still "in service" in the BSP's local APIC when EDK II starts (its EOI never comes), and
  everything at or below its priority class stays blocked, including the EDK II timer (vector
  0x20). HW run 07:35 UTC (2026-11-20): boot stopped at "[Bds]BdsWait(3)", the first wait that
  needs a timer interrupt; rebuild/qemu STALE_LAPIC=1 (vector 0x40 in service, 0x30 pending)
  stops at the same line. Vectors that were pending (IRR) would arrive with no handler once the
  in-service one is cleared, so they are absorbed here too.

  Order: temporary EOI-only handlers on every free vector 0x21-0xFF, log the state, TPR = 0,
  software enable, EOI until ISR is empty, interrupts on for 2 ms so pending vectors are taken
  and EOI'd, temporary handlers removed, state logged again.
**/

#include <PiDxe.h>
#include <Protocol/Cpu.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/LocalApicLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define APIC_TPR  0x80
#define APIC_PPR  0xA0
#define APIC_SVR  0xF0
#define APIC_ISR  0x100
#define APIC_IRR  0x200

#define FIRST_VECTOR  0x21              // 0x20 = LocalApicTimerDxe (already registered: depex)

/* Public LocalApicLib has no raw register access: xAPIC = MMIO at the APIC base, x2APIC = MSR 0x800 + offset/16. */
STATIC
UINT32
ReadApic (
  IN UINTN  Offset
  )
{
  if (GetApicMode () == LOCAL_APIC_MODE_X2APIC) {
    return (UINT32)AsmReadMsr64 (0x800 + (UINT32)(Offset >> 4));
  }

  return MmioRead32 (GetLocalApicBaseAddress () + Offset);
}

STATIC
VOID
WriteApic (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  if (GetApicMode () == LOCAL_APIC_MODE_X2APIC) {
    AsmWriteMsr64 (0x800 + (UINT32)(Offset >> 4), Value);
    return;
  }

  MmioWrite32 (GetLocalApicBaseAddress () + Offset, Value);
}

STATIC UINT32   mHits[256];
STATIC BOOLEAN  mTemp[256];

STATIC
VOID
EFIAPI
Ps4StrayInterrupt (
  IN EFI_EXCEPTION_TYPE  InterruptType,
  IN EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  mHits[(UINT8)InterruptType]++;
  SendApicEoi ();
}

STATIC
BOOLEAN
IsrBusy (
  VOID
  )
{
  UINTN  Index;

  for (Index = 0; Index < 8; Index++) {
    if (ReadApic (APIC_ISR + 0x10 * Index) != 0) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
DumpApic (
  IN CONST CHAR8  *Tag
  )
{
  UINTN  Index;
  UINT32 Value;

  DEBUG ((
    DEBUG_INFO,
    "Ps4ApicFixup %a: SVR %x TPR %x PPR %x\n",
    Tag,
    ReadApic (APIC_SVR),
    ReadApic (APIC_TPR),
    ReadApic (APIC_PPR)
    ));
  for (Index = 0; Index < 8; Index++) {
    Value = ReadApic (APIC_ISR + 0x10 * Index);
    if (Value != 0) {
      DEBUG ((DEBUG_INFO, "Ps4ApicFixup %a:   ISR vectors %x-%x: %08x\n", Tag, Index * 32, Index * 32 + 31, Value));
    }
  }

  for (Index = 0; Index < 8; Index++) {
    Value = ReadApic (APIC_IRR + 0x10 * Index);
    if (Value != 0) {
      DEBUG ((DEBUG_INFO, "Ps4ApicFixup %a:   IRR vectors %x-%x: %08x\n", Tag, Index * 32, Index * 32 + 31, Value));
    }
  }
}

EFI_STATUS
EFIAPI
Ps4ApicFixupEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_CPU_ARCH_PROTOCOL  *Cpu;
  EFI_STATUS             Status;
  BOOLEAN                WasEnabled;
  UINTN                  Vector;
  UINTN                  Eois;
  UINTN                  Taken;

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  WasEnabled = SaveAndDisableInterrupts ();

  for (Vector = FIRST_VECTOR; Vector < 256; Vector++) {
    mTemp[Vector] = !EFI_ERROR (Cpu->RegisterInterruptHandler (Cpu, (EFI_EXCEPTION_TYPE)Vector, Ps4StrayInterrupt));
  }

  DumpApic ("before");

  WriteApic (APIC_TPR, 0);
  WriteApic (APIC_SVR, ReadApic (APIC_SVR) | BIT8);   // APIC software enable

  for (Eois = 0; Eois < 256 && IsrBusy (); Eois++) {
    SendApicEoi ();                                                     // clears the highest ISR bit
  }

  EnableInterrupts ();                                                  // take whatever was pending
  MicroSecondDelay (2000);
  DisableInterrupts ();

  Taken = 0;
  for (Vector = FIRST_VECTOR; Vector < 256; Vector++) {
    if (mHits[Vector] != 0) {
      DEBUG ((DEBUG_INFO, "Ps4ApicFixup: absorbed vector %x x%u\n", Vector, mHits[Vector]));
      Taken += mHits[Vector];
    }

    if (mTemp[Vector]) {
      Cpu->RegisterInterruptHandler (Cpu, (EFI_EXCEPTION_TYPE)Vector, NULL);
    }
  }

  DumpApic ("after");
  DEBUG ((DEBUG_INFO, "Ps4ApicFixup: %u EOI(s) for in-service vectors, %u pending interrupt(s) absorbed\n", Eois, Taken));

  SetInterruptState (WasEnabled);
  return EFI_SUCCESS;
}
