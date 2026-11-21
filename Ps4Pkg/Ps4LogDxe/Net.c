/** @file
  Ps4LogDxe - LAN / DHCP / DNS / netboot tests. Each step is bounded and ends in a PASS/FAIL line.
**/

#include <Uefi.h>
#include <Protocol/Dns4.h>
#include <Protocol/Ip4Config2.h>
#include <Protocol/LoadFile.h>
#include <Protocol/PxeBaseCode.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/SimpleNetwork.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include "Ps4Log.h"

#define DHCP_WAIT_MS  20000
#define DNS_WAIT_MS   15000
#define POLL_MS       100

STATIC
VOID
Ip (
  IN OUT REPORT            *R,
  IN     CONST CHAR8       *Label,
  IN     EFI_IPv4_ADDRESS  *A
  )
{
  RPrint (R, "%a %u.%u.%u.%u", Label, A->Addr[0], A->Addr[1], A->Addr[2], A->Addr[3]);
}

STATIC
BOOLEAN
IsZero4 (
  IN EFI_IPv4_ADDRESS  *A
  )
{
  return (A->Addr[0] | A->Addr[1] | A->Addr[2] | A->Addr[3]) == 0;
}

/* DHCP through EFI_IP4_CONFIG2 (policy -> DHCP, then wait for an address). */
STATIC
BOOLEAN
TestDhcp (
  IN OUT REPORT      *R,
  IN     EFI_HANDLE  Nic
  )
{
  EFI_IP4_CONFIG2_PROTOCOL        *Cfg;
  EFI_IP4_CONFIG2_POLICY          Policy;
  EFI_IP4_CONFIG2_INTERFACE_INFO  *Info;
  EFI_IPv4_ADDRESS                *Dns;
  UINTN                           Size;
  UINTN                           Waited;
  UINTN                           I;
  EFI_STATUS                      Status;
  BOOLEAN                         Ok;

  if (EFI_ERROR (gBS->HandleProtocol (Nic, &gEfiIp4Config2ProtocolGuid, (VOID **)&Cfg))) {
    RPrint (R, "  DHCP: FAIL (no IP4 config on this NIC)\n");
    return FALSE;
  }

  Size   = sizeof (Policy);
  Status = Cfg->GetData (Cfg, Ip4Config2DataTypePolicy, &Size, &Policy);
  if (EFI_ERROR (Status) || (Policy != Ip4Config2PolicyDhcp)) {
    Policy = Ip4Config2PolicyDhcp;
    Status = Cfg->SetData (Cfg, Ip4Config2DataTypePolicy, sizeof (Policy), &Policy);
    RPrint (R, "  DHCP: policy set to DHCP: %r\n", Status);
  }

  Ok   = FALSE;
  Info = NULL;
  for (Waited = 0; Waited <= DHCP_WAIT_MS; Waited += POLL_MS) {
    Size   = 0;
    Status = Cfg->GetData (Cfg, Ip4Config2DataTypeInterfaceInfo, &Size, NULL);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      Info = AllocateZeroPool (Size);
      if ((Info != NULL) && !EFI_ERROR (Cfg->GetData (Cfg, Ip4Config2DataTypeInterfaceInfo, &Size, Info)) &&
          !IsZero4 (&Info->StationAddress))
      {
        Ok = TRUE;
        break;
      }

      if (Info != NULL) {
        FreePool (Info);
        Info = NULL;
      }
    }

    gBS->Stall (POLL_MS * 1000);                     // timer events keep the DHCP state machine going
  }

  if (!Ok) {
    RPrint (R, "  DHCP: FAIL (no lease after %u ms)\n", DHCP_WAIT_MS);
    return FALSE;
  }

  RPrint (R, "  DHCP: PASS after ~%u ms: ", Waited);
  Ip (R, "address", &Info->StationAddress);
  Ip (R, " mask", &Info->SubnetMask);
  for (I = 0; I < Info->RouteTableSize; I++) {
    if (IsZero4 (&Info->RouteTable[I].SubnetAddress) && !IsZero4 (&Info->RouteTable[I].GatewayAddress)) {
      Ip (R, " gateway", &Info->RouteTable[I].GatewayAddress);
    }
  }

  RPrint (R, "\n");
  FreePool (Info);

  Size = 0;
  if (Cfg->GetData (Cfg, Ip4Config2DataTypeDnsServer, &Size, NULL) == EFI_BUFFER_TOO_SMALL) {
    Dns = AllocateZeroPool (Size);
    if ((Dns != NULL) && !EFI_ERROR (Cfg->GetData (Cfg, Ip4Config2DataTypeDnsServer, &Size, Dns))) {
      for (I = 0; I < Size / sizeof (EFI_IPv4_ADDRESS); I++) {
        Ip (R, "  DNS server", &Dns[I]);
        RPrint (R, "\n");
      }
    }

    if (Dns != NULL) {
      FreePool (Dns);
    }
  } else {
    RPrint (R, "  DNS server: none from DHCP\n");
  }

  return TRUE;
}

STATIC
VOID
EFIAPI
SetFlag (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  *(BOOLEAN *)Context = TRUE;
}

/* DNS through EFI_DNS4 with the DHCP-provided servers. */
STATIC
VOID
TestDns (
  IN OUT REPORT        *R,
  IN     EFI_HANDLE    Nic,
  IN     CONST CHAR16  *Name
  )
{
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;
  EFI_HANDLE                    Child;
  EFI_DNS4_PROTOCOL             *Dns;
  EFI_DNS4_CONFIG_DATA          Config;
  EFI_DNS4_COMPLETION_TOKEN     Token;
  BOOLEAN                       Done;
  UINTN                         Waited;
  UINTN                         I;
  EFI_STATUS                    Status;

  if (EFI_ERROR (gBS->HandleProtocol (Nic, &gEfiDns4ServiceBindingProtocolGuid, (VOID **)&Sb))) {
    RPrint (R, "  DNS: FAIL (no DNS4 service on this NIC)\n");
    return;
  }

  Child  = NULL;
  Status = Sb->CreateChild (Sb, &Child);
  if (EFI_ERROR (Status) || EFI_ERROR (gBS->HandleProtocol (Child, &gEfiDns4ProtocolGuid, (VOID **)&Dns))) {
    RPrint (R, "  DNS: FAIL (child: %r)\n", Status);
    return;
  }

  ZeroMem (&Config, sizeof (Config));
  Config.UseDefaultSetting = TRUE;
  Config.EnableDnsCache    = FALSE;
  Config.Protocol          = 17;                      // UDP
  Config.RetryCount        = 2;
  Config.RetryInterval     = 3;
  Status                   = Dns->Configure (Dns, &Config);
  if (EFI_ERROR (Status)) {
    RPrint (R, "  DNS: FAIL (configure: %r)\n", Status);
    Sb->DestroyChild (Sb, Child);
    return;
  }

  Done = FALSE;
  ZeroMem (&Token, sizeof (Token));
  gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, SetFlag, &Done, &Token.Event);
  Status = Dns->HostNameToIp (Dns, (CHAR16 *)Name, &Token);
  if (EFI_ERROR (Status)) {
    RPrint (R, "  DNS: FAIL (%s: %r)\n", Name, Status);
  } else {
    for (Waited = 0; !Done && Waited < DNS_WAIT_MS; Waited += POLL_MS) {
      Dns->Poll (Dns);
      gBS->Stall (POLL_MS * 1000);
    }

    if (!Done) {
      RPrint (R, "  DNS: FAIL (%s: no answer within %u ms)\n", Name, DNS_WAIT_MS);
      Dns->Cancel (Dns, &Token);
    } else if (EFI_ERROR (Token.Status) || (Token.RspData.H2AData == NULL)) {
      RPrint (R, "  DNS: FAIL (%s: %r)\n", Name, Token.Status);
    } else {
      RPrint (R, "  DNS: PASS %s ->", Name);
      for (I = 0; I < Token.RspData.H2AData->IpCount; I++) {
        Ip (R, "", &Token.RspData.H2AData->IpList[I]);
      }

      RPrint (R, "\n");
      if (Token.RspData.H2AData->IpList != NULL) {
        FreePool (Token.RspData.H2AData->IpList);
      }

      FreePool (Token.RspData.H2AData);
    }
  }

  gBS->CloseEvent (Token.Event);
  Dns->Configure (Dns, NULL);
  Sb->DestroyChild (Sb, Child);
}

/* PXE: DHCP discover with PXE options, boot file offer, TFTP size probe. */
STATIC
VOID
TestPxe (
  IN OUT REPORT  *R
  )
{
  EFI_HANDLE                  *H;
  UINTN                       N;
  UINTN                       I;
  EFI_PXE_BASE_CODE_PROTOCOL  *Pxe;
  EFI_PXE_BASE_CODE_PACKET    *Pkt;
  EFI_IP_ADDRESS              Server;
  UINT64                      FileSize;
  CHAR8                       File[129];
  BOOLEAN                     Started;
  EFI_STATUS                  Status;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPxeBaseCodeProtocolGuid, NULL, &N, &H))) {
    RPrint (R, "  PXE: FAIL (no PXE base code instance)\n");
    return;
  }

  for (I = 0; I < N; I++) {
    gBS->HandleProtocol (H[I], &gEfiPxeBaseCodeProtocolGuid, (VOID **)&Pxe);
    Started = FALSE;
    if (!Pxe->Mode->Started) {
      Status = Pxe->Start (Pxe, FALSE);              // IPv4
      if (EFI_ERROR (Status)) {
        RPrint (R, "  PXE[%u]: FAIL (start: %r)\n", I, Status);
        continue;
      }

      Started = TRUE;
    }

    if (!Pxe->Mode->UsingIpv6) {
      Status = Pxe->Dhcp (Pxe, TRUE);
      if (EFI_ERROR (Status)) {
        RPrint (R, "  PXE[%u]: FAIL (DHCP/PXE discover: %r)\n", I, Status);
      } else {
        Pkt = Pxe->Mode->ProxyOfferReceived ? &Pxe->Mode->ProxyOffer : &Pxe->Mode->DhcpAck;
        CopyMem (File, Pkt->Dhcpv4.BootpBootFile, 128);
        File[128] = 0;
        ZeroMem (&Server, sizeof (Server));
        CopyMem (&Server.v4, Pkt->Dhcpv4.BootpSiAddr, 4);
        RPrint (R, "  PXE[%u]: PASS discover (ack %u proxy %u) next-server %u.%u.%u.%u boot file \"%a\"\n", I,
                Pxe->Mode->DhcpAckReceived, Pxe->Mode->ProxyOfferReceived, Server.v4.Addr[0], Server.v4.Addr[1],
                Server.v4.Addr[2], Server.v4.Addr[3], File);
        if (File[0] != 0) {
          FileSize = 0;
          Status   = Pxe->Mtftp (Pxe, EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE, NULL, FALSE, &FileSize, NULL, &Server,
                                 (UINT8 *)File, NULL, FALSE);
          RPrint (R, "  TFTP: %a size of \"%a\": %lu bytes (%r)\n", EFI_ERROR (Status) ? "FAIL" : "PASS", File, FileSize, Status);
        } else {
          RPrint (R, "  TFTP: skipped (no boot file offered; set one on the DHCP/PXE server)\n");
        }
      }
    }

    if (Started) {
      Pxe->Stop (Pxe);
    }
  }

  FreePool (H);
}

STATIC
VOID
ListNetBoot (
  IN OUT REPORT  *R
  )
{
  EFI_HANDLE  *H;
  UINTN       N;
  UINTN       I;
  CHAR16      *Text;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiLoadFileProtocolGuid, NULL, &N, &H))) {
    RPrint (R, "  netboot entries (LoadFile): none\n");
    return;
  }

  for (I = 0; I < N; I++) {
    Text = ConvertDevicePathToText (DevicePathFromHandle (H[I]), FALSE, FALSE);
    RPrint (R, "  netboot entry: %s\n", (Text != NULL) ? Text : L"?");
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  FreePool (H);
}

VOID
ReportNetwork (
  IN OUT REPORT        *R,
  IN     CONST CHAR16  *DnsName
  )
{
  EFI_HANDLE                   *H;
  UINTN                        N;
  UINTN                        I;
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;

  RSection (R, "network: LAN / DHCP / DNS / netboot");
  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleNetworkProtocolGuid, NULL, &N, &H))) {
    RPrint (R, "LAN: FAIL (no network adapter driver bound)\n");
    return;
  }

  for (I = 0; I < N; I++) {
    gBS->HandleProtocol (H[I], &gEfiSimpleNetworkProtocolGuid, (VOID **)&Snp);
    if (gBS->HandleProtocol (H[I], &gEfiIp4Config2ProtocolGuid, (VOID **)&Status) != EFI_SUCCESS) {
      continue;                                     // VLAN/child handles: test the ones with an IP stack
    }

    RPrint (R, "NIC %u: mac %02x:%02x:%02x:%02x:%02x:%02x\n", I,
            Snp->Mode->CurrentAddress.Addr[0], Snp->Mode->CurrentAddress.Addr[1], Snp->Mode->CurrentAddress.Addr[2],
            Snp->Mode->CurrentAddress.Addr[3], Snp->Mode->CurrentAddress.Addr[4], Snp->Mode->CurrentAddress.Addr[5]);
    if (Snp->Mode->MediaPresentSupported) {
      RPrint (R, "  LAN: %a (link %a)\n", Snp->Mode->MediaPresent ? "PASS" : "FAIL", Snp->Mode->MediaPresent ? "up" : "down / no cable");
    } else {
      RPrint (R, "  LAN: link state not reported by the driver\n");
    }

    if (R->Echo) {
      Print (L"ps4log: NIC %u: DHCP (up to %u s), DNS, PXE...\n", I, DHCP_WAIT_MS / 1000);
    }

    if (TestDhcp (R, H[I])) {
      TestDns (R, H[I], DnsName);
    }
  }

  FreePool (H);
  TestPxe (R);
  ListNetBoot (R);
}
