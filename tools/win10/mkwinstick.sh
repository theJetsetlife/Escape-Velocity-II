#!/bin/bash
# mkwinstick.sh <Win10_x64.iso> [out.img] - raw GPT + FAT32 "USB stick" image with Windows 10 Setup
# (sources/install.wim is left out: FAT32 cannot hold it and Setup's first screens do not need it).
# Used by the QEMU harness: qemu/run.sh with WIN_IMG=<out.img> (see docs/WINDOWS10.md).
# Needs: 7z, sgdisk, mkfs.fat, mtools.
set -euo pipefail
ISO=$1; OUT=${2:-$(dirname "$0")/../../out/winstick.img}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
7z x -y -bso0 -bsp0 -o"$T/iso" "$ISO" -x!sources/install.wim
rm -f "$OUT"; truncate -s 2G "$OUT"
sgdisk -q -o -n 1:2048:4194270 -t 1:0700 -c 1:WIN10SETUP "$OUT"
mkfs.fat -F 32 -n WIN10SETUP --offset 2048 "$OUT" $(( (4194270 - 2048 + 1) / 2 )) >/dev/null
mcopy -s -i "$OUT@@1M" "$T"/iso/* ::/
echo "$OUT ready (boot: fs0:\\efi\\boot\\bootx64.efi)"
