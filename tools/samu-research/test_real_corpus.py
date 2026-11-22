#!/usr/bin/env python3
"""Synthetic negative tests for the real-image corpus ingestion path."""
from __future__ import annotations
import hashlib
import tempfile
from pathlib import Path
import unittest
from real_corpus import (CorpusError, FILES, NOR_LENGTH, SLOTS, _slot_digest_in_nor,
                         file_sha256, verify, init)


class RealCorpusTests(unittest.TestCase):
    def test_expected_slot_layout_nonoverlapping(self):
        slots = sorted((off, off + size) for off, size, _ in SLOTS.values())
        for start, end in slots:
            self.assertGreaterEqual(start, 0)
            self.assertLessEqual(end, NOR_LENGTH)
        self.assertTrue(all(a[1] <= b[0] for a, b in zip(slots, slots[1:])))

    def test_nonregular_files_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'file.bin'
            p.symlink_to(Path(__file__))
            with self.assertRaises(CorpusError): file_sha256(p)

    def test_missing_files_rejected(self):
        with tempfile.TemporaryDirectory() as td, self.assertRaises(CorpusError):
            verify(Path(td), strict=False)

    def test_slots_use_correct_offsets(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'nor.bin'
            p.write_bytes(b'ABCD' + bytes(8192))
            self.assertEqual(_slot_digest_in_nor(p, 0, 4), hashlib.sha256(b'ABCD').hexdigest())
            self.assertEqual(_slot_digest_in_nor(p, 1, 3), hashlib.sha256(b'BCD').hexdigest())
            with self.assertRaises(CorpusError): _slot_digest_in_nor(p, 8196, 2)

    def test_corruption_detected_even_without_hash_pin(self):
        with tempfile.TemporaryDirectory() as td:
            c = Path(td)
            (c / 'sflash0.bin').write_bytes(b'\0' * NOR_LENGTH)
            (c / 'kernel_900.bin').write_bytes(b'fake kernel')
            for name, (_, size, _) in SLOTS.items():
                (c / name).write_bytes(b'\0' * size)
            self.assertTrue(verify(c, strict=False)['pass_all'])
            p = c / 'sam_ipl_primary.bin'
            p.write_bytes(b'X' + p.read_bytes()[1:])
            with self.assertRaisesRegex(CorpusError, 'differs from NOR'):
                verify(c, strict=False)

    def test_init_roundtrip_unpinned(self):
        with tempfile.TemporaryDirectory() as td:
            c = Path(td)
            k = c / 'test-kernel'; k.write_bytes(b'not an elf - synthetic')
            n = c / 'nor'; n.write_bytes(b'\x7f' * NOR_LENGTH)
            result = init(k, n, c / 'corpus', strict=False)
            self.assertTrue(result['pass_all'])
            self.assertTrue((c / 'corpus' / 'manifest.json').exists())
            self.assertEqual(len(result['records']), len(FILES))
            with self.assertRaisesRegex(CorpusError, 'SHA-256 mismatch'):
                verify(c / 'corpus', strict=True)


if __name__ == '__main__':
    unittest.main()
