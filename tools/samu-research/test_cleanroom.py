#!/usr/bin/env python3
"""Purely synthetic tests for input, guard and fixture safety."""
from __future__ import annotations
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import unittest
from cleanroom_guard import guard
from fuzz_elf import run as fuzz_run
from synthetic_elf import BASE, PAYLOAD, build
from verify_kernel_900 import (ELF_HEADER, PROGRAM_HEADER, InvalidKernel,
                               load_kernel, parse_elf, virtual_to_file, run)


class CleanroomSecurityTests(unittest.TestCase):
    def test_fixture_has_only_synthetic_markers(self):
        b = build()
        self.assertIn(PAYLOAD, b)
        self.assertNotIn(b'Sony', b)
        self.assertEqual(b[:4], b'\x7fELF')
        self.assertEqual(virtual_to_file(parse_elf(b)['segments'], BASE + 0x200, 1), (0x200, 0))

    def test_reject_zero_length_and_address_wrap(self):
        p = parse_elf(build())
        for va, n in ((BASE + 0x200, 0), (-1, 1), (1 << 64, 1), ((1 << 64) - 1, 4)):
            with self.subTest(va=va, length=n), self.assertRaises(InvalidKernel):
                virtual_to_file(p['segments'], va, n)

    def test_reject_bad_alignment(self):
        b = bytearray(build())
        struct.pack_into('<Q', b, 64 + 48, 3)
        with self.assertRaises(InvalidKernel): parse_elf(b)

    def test_reject_overlapping_executable_segments(self):
        b = bytearray(build())
        PROGRAM_HEADER.pack_into(b, 120, 1, 5, 0x200, BASE + 0x200, 0, 0x100, 0x180, 1)
        p = parse_elf(b)
        with self.assertRaises(InvalidKernel):
            virtual_to_file(p['segments'], BASE + 0x201, 3)

    def test_load_kernel_regular_only(self):
        with tempfile.TemporaryDirectory() as td:
            regular = Path(td) / 'test'
            regular.write_bytes(build())
            self.assertEqual(load_kernel(regular), build())
            link = Path(td) / 'link'
            link.symlink_to(regular)
            with self.assertRaises(InvalidKernel): load_kernel(link)
            if os.name == 'posix':
                fifo = Path(td) / 'fifo'
                os.mkfifo(fifo)
                # Do not open a FIFO (which could block). The guard rejects it.
                r = guard([fifo]); self.assertEqual(r['status'], 'FAIL')

    def test_guard_rejects_proprietary_and_secret_files(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / 'safe.py').write_text('print("ok")\n')
            self.assertEqual(guard([p])['status'], 'PASS')
            (p / 'kernel.bin').write_bytes(b'\x7fELF' + bytes(40))
            self.assertEqual(guard([p])['status'], 'FAIL')
            (p / 'kernel.bin').unlink()
            (p / 'secret.txt').write_text('-----BEGIN ' + 'PRIVATE KEY-----\nnot-a-real-key\n')
            self.assertEqual(guard([p])['status'], 'FAIL')

    def test_guard_rejects_network_import(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'bridge.py'
            p.write_text('import socket\n')
            self.assertEqual(guard([p])['status'], 'FAIL')

    def test_fuzz_is_deterministic(self):
        a = fuzz_run(160, 900)
        self.assertEqual(a, fuzz_run(160, 900))
        self.assertEqual(a['accepted'] + a['rejected'], 160)

    def test_fake_kernel_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            name = Path(td) / 'fake.elf'
            name.write_bytes(build())
            info = run(name)
            self.assertFalse(info['pass_all'])
            self.assertFalse(info['sha256_match'])
            self.assertEqual(info['program_header_count'], 2)

    def test_reject_elf_header_overlap(self):
        b = bytearray(build())
        struct.pack_into('<Q', b, 32, 0)
        with self.assertRaises(InvalidKernel): parse_elf(b)


if __name__ == '__main__':
    unittest.main()
