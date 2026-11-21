#!/bin/bash
# Run the directboot handoff (src/upl_load.c + src/directboot/memmap.c) with the real UEFI
# payload in QEMU. Output in qemu/out/:
#   debug.log    stub + h64 text (debugcon port 0xE9)
#   screen.png   the display after WAIT seconds (EDK II DEBUG text goes to the fb console)
#   fbcon.txt    the fb console descriptor at 0x8000 (magic/version/row/col) after WAIT seconds
# Usage: run.sh [WAIT seconds, default 40]      env: UPL=<elf> (default ../out/upl-ps4.elf),
#   SMP (default 8), STALE_LAPIC=1 (hand off with vector 0x40 in service + 0x30 pending, as a
#   takeover from inside an OrbisOS interrupt handler leaves the LAPIC)
#   IO_TEST=1: add qemu-xhci + usb-kbd + usb-storage (FAT, HELLO.TXT) and a FAT disk (SATA.TXT) on
#   the q35 AHCI, plus an NTFS stick (NTFS.TXT); types "ls fs0:".."ls fs2:" on the USB keyboard
#   LOG_TEST=1 (with IO_TEST+NET_TEST, SHELL_CMDS=ps4log): check ps4log files on the FAT stick image
#   WIN_IMG=<raw image>: USB stick + keyboard only (Windows boot test; type the loader path via SHELL_CMDS)
#   DBFLAGS=<n>: directboot info Flags (1 PS4 DSDT, 2 Sony-shaped MADT, 4 keep PCAT, 8 keep real IOAPIC)
#   NET_TEST=1: virtio-net (modern) on QEMU user net (DHCP/DNS/TFTP bootfile pxeboot.efi)
#   MEM8G=1: -m 8G (q35: 2 GiB low + 6 GiB at 4 GiB, PS4-like); h64 carves 2 GiB VRAM and releases the rest
set -euo pipefail
Q=$(cd "$(dirname "$0")" && pwd)
WAIT=${1:-40}
UPL=${UPL:-$Q/../out/upl-ps4.elf}
make -s -C "$Q"
TSC=$(journalctl -k -b 2>/dev/null | sed -n 's/.*tsc: Refined TSC clocksource calibration: \([0-9]*\)\.\([0-9]*\) MHz.*/\1\2000/p' | tail -1)
TSC=${TSC:-2000000000}
O="$Q/out"
MON="${XDG_RUNTIME_DIR:-/tmp}/dbq-$$.sock"     # unix socket paths must stay < 108 bytes
SER="${XDG_RUNTIME_DIR:-/tmp}/dbs-$$.sock"     # COM1 (IO_TEST): logged to out/serial.log
rm -f "$O/debug.log" "$O/screen.ppm" "$O/screen.png" "$O/fbcon.bin" "$O/fbcon.txt" "$MON"
IO_ARGS=""
NIC_ARGS="-nic none"
if [ -n "${WIN_IMG:-}" ]; then                # Windows boot test: USB keyboard + the image as a USB stick
  IO_ARGS="-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -drive if=none,id=win,format=raw,file=$WIN_IMG -device usb-storage,bus=xhci.0,drive=win"
fi
if [ "${NET_TEST:-0}" = 1 ]; then              # QEMU user net: DHCP, DNS (host resolver), TFTP for PXE
  mkdir -p "$O/tftp"; head -c 4096 /dev/zero > "$O/tftp/pxeboot.efi"
  NIC_ARGS="-netdev user,id=n0,tftp=$O/tftp,bootfile=pxeboot.efi -device virtio-net-pci,disable-legacy=on,netdev=n0"
fi
if [ "${IO_TEST:-0}" = 1 ]; then
  rm -f "$O/usbstick.img" "$O/sata.img" "$O/ntfs.img"
  dd if=/dev/zero of="$O/ntfs.img" bs=1M count=32 status=none
  mkntfs -q -F -f -L NTFSSTICK "$O/ntfs.img" >/dev/null 2>&1
  printf "hello from the ntfs stick\r\n" > "$O/NTFS.TXT"; ntfscp -q "$O/ntfs.img" "$O/NTFS.TXT" NTFS.TXT >/dev/null 2>&1
  mkfs.fat -C -n USBSTICK "$O/usbstick.img" 16384 >/dev/null
  printf "hello from the usb stick\r\n" > "$O/HELLO.TXT"; mcopy -i "$O/usbstick.img" "$O/HELLO.TXT" ::HELLO.TXT
  mkfs.fat -C -n SATADISK "$O/sata.img" 16384 >/dev/null
  printf "hello from the sata disk\r\n" > "$O/SATA.TXT"; mcopy -i "$O/sata.img" "$O/SATA.TXT" ::SATA.TXT
  rm -f "$O/serial.log" "$SER"
  IO_ARGS="-chardev socket,id=ser0,path=$SER,server=on,wait=off,logfile=$O/serial.log -serial chardev:ser0 -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -drive if=none,id=stick,format=raw,file=$O/usbstick.img -device usb-storage,bus=xhci.0,drive=stick -drive if=none,id=nstick,format=raw,file=$O/ntfs.img -device usb-storage,bus=xhci.0,drive=nstick -drive if=none,id=sata0,format=raw,file=$O/sata.img -device ide-hd,drive=sata0,bus=ide.0"
fi
qemu-system-x86_64 -M q35,hpet=off -accel "${ACCEL:-kvm}" -cpu "${QCPU:-host}" -smp "${SMP:-8}" -m "$([ "${MEM8G:-0}" = 1 ] && echo 8G || echo 2G)" -vga std $NIC_ARGS -device intel-hda -device hda-duplex \
  -display none -kernel "$O/stub.elf" -append "upl_size=$(stat -c %s "$UPL") tsc_hz=$TSC dbflags=${DBFLAGS:-0}" \
  -device loader,file="$O/h64.bin",addr=0x01000000,force-raw=on \
  -device loader,file="$UPL",addr=0x00800000,force-raw=on \
  -debugcon file:"$O/debug.log" -action reboot=shutdown,shutdown=pause \
  -monitor unix:"$MON",server,nowait ${QEMU_EXTRA:-} $IO_ARGS \
  $([ "${STALE_LAPIC:-0}" = 1 ] && echo "-device loader,addr=0xff0000,data=0x4c415453,data-len=4") &
QPID=$!
trap 'kill $QPID 2>/dev/null || true; rm -f "$MON" "$SER"' EXIT
for _ in $(seq 1 50); do [ -S "$MON" ] && break; sleep 0.1; done
sleep "$WAIT"
python3 -I - "$MON" "$O" "$SER" <<'MONEOF'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(3)
def cmd(c):
    s.sendall((c + "\n").encode()); time.sleep(1.0)
    out = b""
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            out += d
            if len(d) < 65536: break
    except socket.timeout:
        pass
    return out.decode(errors="replace")
time.sleep(0.3); s.recv(65536)
o = sys.argv[2]
rep = cmd("info status")
regs = cmd("info registers")
import os as _os
if _os.environ.get("IO_TEST") == "1" or _os.environ.get("WIN_IMG"):                 # type on the USB keyboard (usb-kbd)
    keys = {" ": "spc", ":": "shift-semicolon", "\n": "ret", "-": "minus", ".": "dot", "\\": "backslash", "/": "slash"}
    cmds = _os.environ.get("SHELL_CMDS")             # optional: ";"-separated lines, lowercase
    lines = [c + "\n" for c in cmds.split(";")] if cmds else ["ls fs0:\n", "ls fs1:\n", "ls fs2:\n"]
    for line in lines:
        for ch in line:
            cmd("sendkey " + keys.get(ch, ch)); time.sleep(0.12)
        time.sleep(float(_os.environ.get("SHELL_WAIT", "2.5")))
    try:                                               # serial ConIn: type over COM1
        import threading
        sc = socket.socket(socket.AF_UNIX); sc.connect(sys.argv[3])
        def _drain():                                  # never let the socket back-pressure the UART
            try:
                while sc.recv(65536): pass
            except OSError: pass
        threading.Thread(target=_drain, daemon=True).start()
        for ch in b"echo SERIALOK\r":
            sc.send(bytes([ch])); time.sleep(0.05)
        time.sleep(3)
    except OSError as e:
        print("serial socket:", e)
cmd("screendump " + o + "/screen.ppm")
cmd("pmemsave 0x8000 56 " + sys.argv[1] + ".fbcon")   # short path: long ones fail in HMP
import shutil, os
for _ in range(50):                                   # pmemsave lands asynchronously
    if os.path.exists(sys.argv[1] + ".fbcon") and os.path.getsize(sys.argv[1] + ".fbcon") >= 56: break
    time.sleep(0.1)
if os.path.exists(sys.argv[1] + ".fbcon"): shutil.move(sys.argv[1] + ".fbcon", o + "/fbcon.bin")
import re
extra = ""
mi = re.search(r"IDT=\s+([0-9a-f]+)", regs); mt = re.search(r"TR =\S+ ([0-9a-f]+)", regs)
if mi:
    idt = int(mi.group(1), 16)
    for v in (0x02, 0x08, 0x0e, 0x20, 0x30, 0x40):
        extra += "IDT[%#04x] " % v + cmd("xp /2gx %#x" % (idt + 16 * v)) + "\n"
if mt:
    tss = int(mt.group(1), 16)
    extra += "TSS " + cmd("xp /13gx %#x" % tss) + "\n"
open(o + "/monitor.txt", "w").write(rep + "\n" + regs + "\n" + extra)
print([l for l in rep.splitlines() if "VM status" in l])
print([l.strip() for l in regs.splitlines() if l.startswith(("RIP", "CR0", "EFER"))][:4])
s.close()
MONEOF
sleep 1
python3 -I - "$O" <<'EOF'
import struct, sys, os
o = sys.argv[1]
try:
    from PIL import Image
    Image.open(os.path.join(o, "screen.ppm")).save(os.path.join(o, "screen.png"))
except Exception as e:
    print("screenshot conversion failed:", e)
if not os.path.exists(os.path.join(o, "fbcon.bin")):
    print("fbcon descriptor dump missing (pmemsave)"); b = bytes(56)
else:
    b = open(os.path.join(o, "fbcon.bin"), "rb").read()
magic, ver, fb, w, h, pitch, col, row = struct.unpack_from("<IIQIIIII", b)
txt = "magic=%#x version=%d fb=%#x %dx%d pitch=%d cursor row=%d col=%d" % (magic, ver, fb, w, h, pitch, row, col)
open(os.path.join(o, "fbcon.txt"), "w").write(txt + "\n"); print(txt)
EOF
echo "--- debug.log"; cat "$O/debug.log"
echo "screen: $O/screen.png"
if [ "${IO_TEST:-0}" = 1 ]; then
  for want in "Ps4SbIo: .*xHCI BAR0 .*: Success" "Ps4SbIo: .*AHCI BAR5 .*: Success"; do
    grep -q -E "$want" "$O/debug.log" || { mkdir -p "$O/fail-io"; cp -f "$O"/debug.log "$O"/screen.png "$O/fail-io/" 2>/dev/null; echo "RESULT: FAIL (IO_TEST: missing [$want])"; exit 1; }
  done
  grep -q "Ps4SbIo:" "$O/serial.log" || { echo "RESULT: FAIL (IO_TEST: DEBUG text not mirrored to the UART)"; exit 1; }
  python3 -I -c 'import re,sys; t=re.sub(r"\x1b\[[0-9;]*[A-Za-z]","",open(sys.argv[1],"rb").read().decode("latin1")); sys.exit(0 if any(l.strip()=="SERIALOK" for l in t.splitlines()) else 1)' "$O/serial.log" \
    || { echo "RESULT: FAIL (IO_TEST: no SERIALOK output line on the serial console)"; exit 1; }
  if [ -z "${SHELL_CMDS:-}" ]; then
    for f in HELLO.TXT SATA.TXT NTFS.TXT; do
      grep -a -q "$f" "$O/serial.log" || { echo "RESULT: FAIL (IO_TEST: $f not listed by the Shell: FAT USB / SATA / NTFS USB)"; exit 1; }
    done
  fi
  echo "IO_TEST: registrations, UART mirror, serial input, and HELLO.TXT/SATA.TXT/NTFS.TXT listings OK"
fi
if [ "${LOG_TEST:-0}" = 1 ]; then              # ps4log files written to the FAT stick, checked on the host
  rm -rf "$O/ps4log"; mkdir -p "$O/ps4log"
  mcopy -s -n -i "$O/usbstick.img" ::ps4log "$O/" 2>/dev/null || true
  ls -la "$O/ps4log"
  B=$(ls "$O"/ps4log/boot-*.txt 2>/dev/null | head -1); C=$(ls "$O"/ps4log/2*.txt 2>/dev/null | head -1)
  [ -n "$B" ] || { echo "RESULT: FAIL (LOG_TEST: no auto-saved boot-*.txt)"; exit 1; }
  [ -n "$C" ] || { echo "RESULT: FAIL (LOG_TEST: no ps4log command output file)"; exit 1; }
  for w in "boot log" "UEFI memory map" "== ACPI" "PCI bus 0" "== cpu" "== devices"; do
    grep -q "$w" "$B" || { echo "RESULT: FAIL (LOG_TEST: boot file lacks [$w])"; exit 1; }
  done
  for w in "LAN: PASS" "DHCP: PASS" "DNS: PASS" "PXE\[0\]: PASS" "TFTP: PASS"; do
    grep -q "$w" "$C" || { echo "RESULT: FAIL (LOG_TEST: command file lacks [$w])"; grep -E "LAN:|DHCP:|DNS:|PXE|TFTP:" "$C"; exit 1; }
  done
  echo "LOG_TEST: auto-save $(basename "$B") + command $(basename "$C"): sections and LAN/DHCP/DNS/PXE/TFTP PASS"
fi
if grep -q "\[Bds\]Booting UEFI Shell" "$O/debug.log" && ! grep -q -E "X64 Exception|ASSERT" "$O/debug.log"; then
  echo "RESULT: PASS (UEFI Shell reached, no exception/ASSERT, SMP=${SMP:-8})"; exit 0
fi
F="$O/fail-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$F"; cp -f "$O"/debug.log "$O"/screen.png "$O"/monitor.txt "$O"/serial.log "$F"/ 2>/dev/null || true
echo "RESULT: FAIL (no UEFI Shell, or exception/ASSERT in debug.log; SMP=${SMP:-8}; evidence kept in $F)"; exit 1
