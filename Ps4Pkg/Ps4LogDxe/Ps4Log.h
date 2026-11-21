/** @file
  Ps4LogDxe - shared declarations (report buffer, sections).
**/

#ifndef PS4_LOG_H_
#define PS4_LOG_H_

#include <Uefi.h>

typedef struct {
  CHAR8    *Buf;
  UINTN    Len;
  UINTN    Cap;
  BOOLEAN  Echo;            // also print section progress on the console
} REPORT;

/** Append text; "\n" not preceded by "\r" becomes "\r\n" (readable on Windows too). */
VOID
RAppend (
  IN OUT REPORT      *R,
  IN     CONST CHAR8 *Text,
  IN     UINTN       Length
  );

VOID
EFIAPI
RPrint (
  IN OUT REPORT      *R,
  IN     CONST CHAR8 *Format,
  ...
  );

/** "== Title ==" header (and a console progress line when R->Echo). */
VOID
RSection (
  IN OUT REPORT      *R,
  IN     CONST CHAR8 *Title
  );

/** Hex dump of Length bytes of MMIO/memory starting at Address (32-bit reads). */
VOID
RHexDump32 (
  IN OUT REPORT  *R,
  IN     UINTN   Address,
  IN     UINTN   Length
  );

VOID
ReportBootLog (
  IN OUT REPORT  *R
  );

VOID
ReportHardware (
  IN OUT REPORT  *R
  );

/** LAN / DHCP / DNS / netboot tests on every NIC. DnsName: host name to resolve. */
VOID
ReportNetwork (
  IN OUT REPORT        *R,
  IN     CONST CHAR16  *DnsName
  );

#endif
