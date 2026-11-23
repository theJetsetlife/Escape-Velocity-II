#!/usr/bin/env python3
"""Host tests for a synthetic historical/hardened key-length model."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from samu_hmac_policy import (  # noqa: E402
    HmacPolicy,
    KeyLengthRejected,
    MINIMUM_HARDENED_LENGTH,
    build_report,
)


class SamuHmacPolicyTests(unittest.TestCase):
    def setUp(self):
        self.legacy = HmacPolicy("historical_unchecked_model", 1)
        self.hardened = HmacPolicy("hardened_length_check_model", 16)

    def test_short_requests_diverge(self):
        for n in (1, 15):
            with self.subTest(length=n):
                self.assertEqual(len(self.legacy.digest(n)), 32)
                with self.assertRaises(KeyLengthRejected):
                    self.hardened.digest(n)

    def test_both_accept_minimum_and_full_length(self):
        for n in (MINIMUM_HARDENED_LENGTH, 32):
            with self.subTest(length=n):
                self.assertEqual(self.legacy.digest(n), self.hardened.digest(n))

    def test_bounded_and_typed_lengths(self):
        for n in (0, -1, 33, 1024):
            with self.subTest(length=n):
                with self.assertRaises(KeyLengthRejected):
                    self.legacy.digest(n)
        for n in ("16", None, 1.5, True):
            with self.subTest(length=n):
                with self.assertRaises(TypeError):
                    self.legacy.digest(n)

    def test_deterministic_public_fixture(self):
        self.assertEqual(self.hardened.digest(16), self.hardened.digest(16))
        self.assertNotEqual(self.legacy.digest(1), self.legacy.digest(16))

    def test_report_does_not_export_keys_or_digests(self):
        report = build_report()
        self.assertEqual(len(report["results"]), 4)
        self.assertEqual(report["results"][0]["historical_unchecked_model"], "accepted")
        self.assertEqual(report["results"][0]["hardened_length_check_model"], "rejected")
        self.assertEqual(
            set(report["results"][0]),
            {"requested_length", "historical_unchecked_model", "hardened_length_check_model"},
        )


if __name__ == "__main__":
    unittest.main()
