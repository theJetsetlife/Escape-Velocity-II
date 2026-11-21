#!/bin/bash
# Build upl-ps4.elf from scratch: pinned upstream edk2 (./edk2 from scripts/setup-edk2.sh, unmodified) + Ps4Pkg.
# Output: out/upl-ps4.elf (+ .sha256, build log). EDK2=<path> selects another edk2 checkout.
#
# Platform flags = the values an earlier PS4 UEFI shell build proved on the Fat: one logical CPU, LAPIC timer, no serial driver/terminal. PcdFSBClock = LAPIC timer
# input clock measured there (99.604 MHz). PcdSystemMemoryUefiRegionSize = 12 MiB: the UPL entry takes its
# first block from one 16 MiB directboot UEFI-heap chunk (the only RAM EDK II is given).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
EDK2=${EDK2:-$ROOT/edk2}
[ -d "$EDK2/BaseTools" ] || { echo "no edk2 at $EDK2: run scripts/setup-edk2.sh (make setup)"; exit 1; }
mkdir -p "$ROOT/out"

python3 -I "$HERE/mkplatform.py" "$EDK2"

cd "$EDK2"
rm -f Build/UefiPayloadPkgX64/DEBUG_GCC/FV/NETWORKFV.Fv   # stale; the PS4 build carries network in PS4COMPFV
export WORKSPACE="$EDK2"
export PACKAGES_PATH="$EDK2:$ROOT"
# The drive can mount under a different path; edk2 Conf/BuildEnv.sh caches the old absolute
# EDK_TOOLS_PATH and only uses it when unset, so set it from this tree (2026-11-20).
export EDK_TOOLS_PATH="$EDK2/BaseTools"
export PYTHONDONTWRITEBYTECODE=1
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct)}
set +u; source edksetup.sh >/dev/null; set -u

python3 UefiPayloadPkg/UniversalPayloadBuild.py \
  -t GCC -a X64 -b DEBUG \
  -c Ps4Pkg/Ps4Payload.dsc \
  -D MAX_LOGICAL_PROCESSORS=1 \
  -D TIMER_SUPPORT=LAPIC \
  -D SERIAL_DRIVER_ENABLE=TRUE \
  -D DISABLE_SERIAL_TERMINAL=FALSE \
  -D RAM_DISK_ENABLE=FALSE \
  -D NETWORK_DRIVER_ENABLE=TRUE \
  -D NETWORK_TLS_ENABLE=FALSE \
  -D NETWORK_IP6_ENABLE=FALSE \
  -D NETWORK_VLAN_ENABLE=FALSE \
  -D NETWORK_ALLOW_HTTP_CONNECTIONS=TRUE \
  -D PS2_MOUSE_ENABLE=FALSE \
  -p gEfiMdePkgTokenSpaceGuid.PcdFSBClock=99604000 \
  -p gEfiMdePkgTokenSpaceGuid.PcdEnforceSecureRngAlgorithms=FALSE \
  -p gUefiPayloadPkgTokenSpaceGuid.PcdSystemMemoryUefiRegionSize=0x00C00000 \
  "$@" 2>&1 | tee "$ROOT/out/upl-ps4-build.log"

cp Build/UefiPayloadPkgX64/UniversalPayload.elf "$ROOT/out/upl-ps4.elf"
cd "$ROOT/out" && sha256sum upl-ps4.elf | tee upl-ps4.elf.sha256
