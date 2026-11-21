#!/bin/bash
# deploy.sh - put the built stage into the WebKitty GoldHEN slot and check what the PS4 will get.
#   stage -> $WEBKITTY_DIR/includes/payloads/HEN/goldhen_v2.4b18.14.bin (payload file only; no WebKitty code)
#   UPL   -> out/upl-ps4.elf is served directly by the receiver container (server/compose.yml mounts out/)
# The slot's previous content is kept in backups/served-<time>/ first. Settings: server/.env.
#   tools/deploy.sh [stage.bin] [upl.elf]      default: src/directboot/directboot.bin, out/upl-ps4.elf
# A stage has the PC address built in (make stage reads PC_IP/PC_PORT from server/.env); the prebuilt
# stages under prebuilt/ were built for 10.0.0.71:9031 - retarget one with
#   python3 tools/diag_recv.py patch <stage.bin> --ip <PC_IP> --port <PC_PORT>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/server/.env" ] || { echo "create server/.env from server/.env.example first"; exit 1; }
set -a; . "$ROOT/server/.env"; set +a
STAGE=${1:-$ROOT/src/directboot/directboot.bin}
UPL=${2:-$ROOT/out/upl-ps4.elf}
SLOT="$WEBKITTY_DIR/includes/payloads/HEN/goldhen_v2.4b18.14.bin"
[ -f "$STAGE" ] && [ -f "$UPL" ] || { echo "missing $STAGE or $UPL (make stage upl, or use prebuilt/)"; exit 1; }
[ -f "$SLOT" ] || { echo "no GoldHEN slot at $SLOT (check WEBKITTY_DIR)"; exit 1; }
B="$ROOT/backups/served-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$B"; cp -a "$SLOT" "$B/slot.bin"
cp "$STAGE" "$SLOT"
mkdir -p "$ROOT/out"
[ "$(realpath "$UPL")" = "$(realpath -m "$ROOT/out/upl-ps4.elf")" ] || cp "$UPL" "$ROOT/out/upl-ps4.elf"
want_s=$(sha256sum "$STAGE" | cut -c1-16); want_u=$(sha256sum "$ROOT/out/upl-ps4.elf" | cut -c1-16)
got_s=$(curl -s "http://127.0.0.1:${WEB_PORT:-9898}/includes/payloads/HEN/goldhen_v2.4b18.14.bin" | sha256sum | cut -c1-16)
got_u=$(docker exec diag-receiver sha256sum /upl/upl-ps4.elf 2>/dev/null | cut -c1-16 || true)
echo "stage  want $want_s served $got_s"; echo "UPL    want $want_u served ${got_u:-<receiver not running>}"
echo "previous slot kept in $B"
[ "$want_s" = "$got_s" ] && [ "$want_u" = "$got_u" ] && echo "DEPLOYED" || { echo "MISMATCH (are the containers up? make server)"; exit 1; }
