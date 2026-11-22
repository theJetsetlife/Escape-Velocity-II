#!/usr/bin/env python3
"""Create a wholly synthetic ELF64 FreeBSD test fixture (not PS4 code).

The instruction markers are ASCII from this source, not machine bytes taken
from a Sony kernel. Fixtures live only in a temporary directory or stdout.
"""
from __future__ import annotations
import argparse
from pathlib import Path
from verify_kernel_900 import ELF_HEADER, PROGRAM_HEADER, PF_X, PT_LOAD

BASE = 0x0000000040000000
PAYLOAD = b'OFFLINE-SYNTHETIC-SAMU-MOCK-BYTES-DO-NOT-EXECUTE!'


def build(executable: bool = True) -> bytes:
    image = bytearray(1024)
    identity = b'\x7fELF\x02\x01\x01\x09' + bytes(8)
    image[:ELF_HEADER.size] = ELF_HEADER.pack(
        identity, 2, 62, 1, BASE + 0x200, 64, 0, 0, 64, 56, 2, 0, 0, 0)
    # Deliberately, file offset does not equal vaddr - ELF base.
    image[64:120] = PROGRAM_HEADER.pack(PT_LOAD, (5 if executable else 4),
                                       0x200, BASE + 0x200, 0, 0x100, 0x180, 1)
    # BSS gap is intentional; do not map file bytes through it.
    image[120:176] = PROGRAM_HEADER.pack(PT_LOAD, 6, 0x300,
                                        BASE + 0x800, 0, 0x80, 0x100, 1)
    image[0x200:0x200 + len(PAYLOAD)] = PAYLOAD
    image[0x300:0x310] = b'SYNTHETIC-DATA!!'
    return bytes(image)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True, help='Temporary fixture output path')
    args = ap.parse_args()
    Path(args.out).write_bytes(build())
