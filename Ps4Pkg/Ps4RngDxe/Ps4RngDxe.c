/** @file
  Ps4RngDxe - EFI_RNG_PROTOCOL for the PS4 firmware, NOT cryptographic.

  NetworkPkg links DxeRngLib (depex gEfiRngProtocolGuid), so no network driver - nor Ps4YukonDxe,
  through NetLib - is dispatched without an RNG protocol. SecurityPkg RngDxe on X64 needs RDRAND,
  which the PS4's Jaguar cores do not have. This driver produces the protocol from a SplitMix64
  generator seeded and re-mixed with the TSC on every call, and advertises it honestly as
  gEdkiiRngAlgorithmUnSafe; DxeRngLib then uses GetRNG (default). Good enough for DHCP/TCP/DNS
  identifiers in firmware; TLS is not built into this payload.
**/

#include <Uefi.h>
#include <Protocol/Rng.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT64  mState;

STATIC
UINT64
Next (
  VOID
  )
{
  UINT64  Z;

  mState += 0x9E3779B97F4A7C15ULL ^ AsmReadTsc ();          // fresh TSC jitter on every draw
  Z       = mState;
  Z       = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  Z       = (Z ^ (Z >> 27)) * 0x94D049BB133111EBULL;
  return Z ^ (Z >> 31);
}

STATIC
EFI_STATUS
EFIAPI
Ps4RngGetInfo (
  IN     EFI_RNG_PROTOCOL   *This,
  IN OUT UINTN              *RNGAlgorithmListSize,
  OUT    EFI_RNG_ALGORITHM  *RNGAlgorithmList
  )
{
  if ((This == NULL) || (RNGAlgorithmListSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (*RNGAlgorithmListSize < sizeof (EFI_RNG_ALGORITHM)) {
    *RNGAlgorithmListSize = sizeof (EFI_RNG_ALGORITHM);
    return EFI_BUFFER_TOO_SMALL;
  }

  if (RNGAlgorithmList == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CopyGuid (RNGAlgorithmList, &gEdkiiRngAlgorithmUnSafe);
  *RNGAlgorithmListSize = sizeof (EFI_RNG_ALGORITHM);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Ps4RngGetRNG (
  IN  EFI_RNG_PROTOCOL   *This,
  IN  EFI_RNG_ALGORITHM  *RNGAlgorithm  OPTIONAL,
  IN  UINTN              RNGValueLength,
  OUT UINT8              *RNGValue
  )
{
  UINT64  V;
  UINTN   N;

  if ((This == NULL) || (RNGValueLength == 0) || (RNGValue == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((RNGAlgorithm != NULL) && !CompareGuid (RNGAlgorithm, &gEdkiiRngAlgorithmUnSafe)) {
    return EFI_UNSUPPORTED;                                // never claims an SP800-90 / raw TRNG
  }

  while (RNGValueLength > 0) {
    V = Next ();
    N = (RNGValueLength < sizeof (V)) ? RNGValueLength : sizeof (V);
    CopyMem (RNGValue, &V, N);
    RNGValue       += N;
    RNGValueLength -= N;
  }

  return EFI_SUCCESS;
}

STATIC EFI_RNG_PROTOCOL  mRng = { Ps4RngGetInfo, Ps4RngGetRNG };

EFI_STATUS
EFIAPI
Ps4RngEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;
  EFI_STATUS  Status;
  UINTN       I;

  mState = AsmReadTsc () ^ (UINT64)(UINTN)&Handle ^ ((UINT64)(UINTN)SystemTable << 17);
  for (I = 0; I < 8; I++) {
    Next ();
  }

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle, &gEfiRngProtocolGuid, &mRng, NULL);
  DEBUG ((DEBUG_INFO, "Ps4Rng: EFI_RNG_PROTOCOL (TSC-seeded, not cryptographic): %r\n", Status));
  return Status;
}
