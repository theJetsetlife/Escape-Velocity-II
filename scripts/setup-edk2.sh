#!/bin/bash
# setup-edk2.sh - fetch the pinned, UNMODIFIED upstream edk2 into ./edk2 and build its BaseTools.
# All PS4 changes live in Ps4Pkg/ (Ps4Pkg/mkplatform.py derives Ps4Payload.dsc/.fdf from upstream
# UefiPayloadPkg by exact-match edits, so a different edk2 commit can break that step on purpose).
#
#   scripts/setup-edk2.sh                 clone from GitHub
#   EDK2_URL=/path/to/edk2 scripts/setup-edk2.sh   clone from a local mirror instead (faster, offline)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EDK2_URL=${EDK2_URL:-https://github.com/tianocore/edk2.git}
EDK2_COMMIT=2970e5699ba6267f3384ffab20f96647578aebc8
DEST="$ROOT/edk2"

if [ ! -d "$DEST/.git" ]; then
  git clone "$EDK2_URL" "$DEST"
fi
cd "$DEST"
git fetch -q origin "$EDK2_COMMIT" 2>/dev/null || true
git checkout -q "$EDK2_COMMIT"
# submodules: from upstream URLs (a local mirror's submodules are used when present)
git submodule sync -q
for sm in $(git config -f .gitmodules --get-regexp 'submodule\..*\.path' | awk '{print $2}'); do
  if [ -d "$EDK2_URL/$sm/.git" ] || [ -f "$EDK2_URL/$sm/.git" ]; then
    git config "submodule.$sm.url" "$EDK2_URL/$sm"
  fi
done
# file transport must be allowed explicitly (git >= 2.38) when the submodules come from a local mirror
git -c protocol.file.allow=always submodule update --init --recursive -q || {
  echo "submodule update FAILED (network? mirror without submodules?) - edk2 cannot build without them"; exit 1; }
[ -n "$(git status --porcelain --ignore-submodules)" ] && { echo "edk2 tree is not clean - refusing"; exit 1; }
make -s -C BaseTools -j"$(nproc)"
echo "edk2 ready at $DEST ($(git rev-parse --short HEAD))"
