#!/bin/bash
# wintry.sh <DBFLAGS> <label> - boot the Windows 10 Setup stick in the QEMU harness, report the VM status and,
# on a bugcheck, the KiBugCheckData record read from a RAM dump (see docs/TESTING.md section 5).
#   WIN_IMG (default out/winstick.img, from mkwinstick.sh), SW = seconds to wait after launching Setup (180)
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); Q=$ROOT/qemu; T=$(dirname "$0")
WIN_IMG=${WIN_IMG:-$ROOT/out/winstick.img}; mkdir -p "$Q/out"
QM=${XDG_RUNTIME_DIR:-/tmp}/qmp-win-$2.sock; RAM=$Q/out/ram-$2.bin; rm -f "$QM" "$RAM"
python3 -I "$T/qmpdump.py" "$QM" "$RAM" > "$Q/out/qmp-$2.log" 2>&1 & LP=$!
(cd "$Q" && QEMU_EXTRA="-qmp unix:$QM,server,nowait" QCPU=host,hv-crash DBFLAGS=$1 WIN_IMG="$WIN_IMG" \
  SHELL_CMDS='fs0:\efi\boot\bootx64.efi' SHELL_WAIT=${SW:-180} timeout 420 ./run.sh 40 > "out/run-$2.txt" 2>&1)
kill $LP 2>/dev/null; cp "$Q/out/screen.png" "$Q/out/win-$2.png"
echo "== $2 (DBFLAGS=$1): $(grep -a 'Ps4Acpi:' "$Q/out/debug.log" | cut -c1-110 | tr '\n' ' ')"
echo "   $(grep -i 'VM status' "$Q/out/run-$2.txt" | head -1)   screen: qemu/out/win-$2.png"
sleep 5; [ -f "$RAM" ] && python3 -I "$T/kibc.py" "$RAM"; rm -f "$RAM"
