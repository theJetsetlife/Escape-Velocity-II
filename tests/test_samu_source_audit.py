#!/usr/bin/env python3
"""Tests for compile-only source audit using invented C fixtures."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "samu_source_audit.py"


class SourceAuditTests(unittest.TestCase):
    def test_can_compile_synthetic_c_and_record_undefined_symbol(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "sample.c").write_text(
                "extern int external_service(int);\nint example(int n){return external_service(n);}\n"
            )
            (root / "compat.h").write_text("#pragma once\n")
            r = subprocess.run(
                [sys.executable, str(SCRIPT), "--source", str(root / "sample.c"),
                 "--header", str(root / "compat.h"), "--output-dir", str(root / "out")],
                text=True, capture_output=True,
            )
            self.assertEqual(r.returncode, 0, r.stderr)
            report = json.loads((root / "out" / "build-audit.json").read_text())
            self.assertIn("external_service", report["undefined_symbols"])
            self.assertTrue(report["compile_passed"])
            self.assertFalse(report["link_performed"])
            self.assertFalse(report["runnable_ps4_payload"])
            self.assertTrue((root / "out" / "source-compilation-only.o").exists())

    def test_rejects_non_c_source(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "sample.txt").write_text("test")
            (root / "compat.h").write_text("#pragma once\n")
            r = subprocess.run(
                [sys.executable, str(SCRIPT), "--source", str(root / "sample.txt"),
                 "--header", str(root / "compat.h"), "--output-dir", str(root / "out")],
                text=True, capture_output=True,
            )
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("expected a readable .c file", r.stderr)
            self.assertFalse((root / "out" / "source-compilation-only.o").exists())


if __name__ == "__main__":
    unittest.main()
