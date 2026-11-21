#!/bin/bash
# matrix.sh - the QEMU regression gate. Every configuration must print RESULT: PASS before a PS4 run.
# Optional: WIN_IMG=<stick image> also boots Windows 10 Setup (tools/win10/mkwinstick.sh) and checks the
# VM is still running (no bugcheck) after 150 s.
set -uo pipefail
Q=$(cd "$(dirname "$0")" && pwd); cd "$Q"
fail=0
run() { local label=$1; shift
  env "$@" timeout 420 ./run.sh "${WAIT:-50}" > out/matrix-$label.txt 2>&1
  local r; r=$(grep -a "RESULT" out/matrix-$label.txt | tail -1)
  echo "$label: ${r:-no RESULT line}"; case "$r" in *PASS*) ;; *) fail=1;; esac; }
mkdir -p out
run smp8 SMP=8
run smp1 SMP=1
run stale-lapic STALE_LAPIC=1
run mem8g MEM8G=1
run io IO_TEST=1
run net-log MEM8G=1 NET_TEST=1 IO_TEST=1 LOG_TEST=1 SHELL_CMDS=ps4log SHELL_WAIT=45
run ps4-dsdt DBFLAGS=1 MEM8G=1
if [ -n "${WIN_IMG:-}" ]; then
  DBFLAGS=1 MEM8G=1 WIN_IMG="$WIN_IMG" SHELL_CMDS='fs0:\efi\boot\bootx64.efi' SHELL_WAIT=150 timeout 420 ./run.sh 40 > out/matrix-win10.txt 2>&1
  s=$(grep -a -i "VM status" out/matrix-win10.txt | head -1); echo "win10-setup: $s (screen: out/screen.png)"
  case "$s" in *running*) ;; *) fail=1;; esac
fi
[ $fail = 0 ] && echo "MATRIX: PASS" || { echo "MATRIX: FAIL (logs: qemu/out/matrix-*.txt, failed runs keep qemu/out/fail-*)"; exit 1; }
