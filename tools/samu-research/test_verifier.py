#!/usr/bin/env python3
"""Offline regression tests for the ELF-aware instruction verifier."""
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

from verify_kernel_900 import (InvalidKernel, parse_elf, virtual_to_file,
                               PROGRAM_HEADER, ELF_HEADER, PT_LOAD, PF_X, run)


def make_elf(executable=True):
    image = bytearray(0x200)
    image[:ELF_HEADER.size] = ELF_HEADER.pack(
        b'\x7fELF\x02\x01\x01\x09' + bytes(8), 2, 62, 1,
        0x1000, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    image[64:120] = PROGRAM_HEADER.pack(PT_LOAD, 5 if executable else 4,
                                        0x100, 0x1000, 0x1000, 0x10, 0x20, 0x1000)
    image[0x100:0x110] = b'\x90' * 16
    return image


class ElfVerifierTests(unittest.TestCase):
    def test_parses_program_header_count_correctly(self):
        result = parse_elf(make_elf())
        self.assertEqual(result['phnum'], 1)
        self.assertEqual(result['phentsize'], 56)
        self.assertEqual(result['segments'][0]['offset'], 0x100)

    def test_uses_segment_file_offsets_not_base_subtraction(self):
        parsed = parse_elf(make_elf())
        self.assertEqual(virtual_to_file(parsed['segments'], 0x1002, 3), (0x102, 0))

    def test_rejects_non_executable_mapping(self):
        parsed = parse_elf(make_elf(False))
        with self.assertRaises(InvalidKernel):
            virtual_to_file(parsed['segments'], 0x1000, 1)

    def test_rejects_bss_and_boundary_crossing(self):
        parsed = parse_elf(make_elf())
        with self.assertRaises(InvalidKernel):
            virtual_to_file(parsed['segments'], 0x1010, 1)
        with self.assertRaises(InvalidKernel):
            virtual_to_file(parsed['segments'], 0x100f, 2)

    def test_rejects_corrupt_phentsize(self):
        image = make_elf()
        struct.pack_into('<H', image, 54, 6)
        with self.assertRaises(InvalidKernel):
            parse_elf(image)

    def test_rejects_truncated_program_header_table(self):
        image = make_elf()
        struct.pack_into('<Q', image, 32, 0x1f0)
        with self.assertRaises(InvalidKernel):
            parse_elf(image)

    def test_rejects_impossible_file_extent(self):
        image = make_elf()
        struct.pack_into('<Q', image, 64 + 32, len(image) + 1)
        with self.assertRaises(InvalidKernel):
            parse_elf(image)

    def test_rejects_wrong_architecture(self):
        image = make_elf()
        struct.pack_into('<H', image, 18, 183)
        with self.assertRaises(InvalidKernel):
            parse_elf(image)

    def test_fake_elf_does_not_get_passing_sha(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'test.elf'
            p.write_bytes(make_elf())
            report = run(p)
            self.assertFalse(report['pass_all'])
            self.assertFalse(report['sha256_match'])
            self.assertEqual(report['program_header_count'], 1)
            self.assertEqual(report['passed'], 0)


if __name__ == '__main__':
    unittest.main()
