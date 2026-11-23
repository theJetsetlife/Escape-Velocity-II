#!/usr/bin/env python3
"""Synthetic negative checks for SAMU route separation and evidence provenance."""
import copy
import json
from pathlib import Path
import unittest
from verify_topology import validate

ROOT = Path(__file__).resolve().parent


def load():
    return [json.loads((ROOT / name).read_text(encoding="utf-8"))
            for name in ("transport_topology.json", "evidence.json", "call_graph.json")]


class TransportTopologyTests(unittest.TestCase):
    def test_baseline(self):
        self.assertEqual(validate(*load())["routes"], 2)

    def test_reject_ordinary_sbl_routed_through_icc(self):
        t, e, g = load()
        t["routes"][0]["nodes"].insert(2, "aeolia_icc")
        with self.assertRaisesRegex(ValueError, "SBL route"):
            validate(t, e, g)

    def test_reject_protected_storage_labeled_local_capture(self):
        t, e, g = load()
        t["routes"][1]["evidence_level"] = "verified_host_instructions_only"
        with self.assertRaisesRegex(ValueError, "SNVS route"):
            validate(t, e, g)

    def test_reject_mismatched_kernel(self):
        t, e, g = load()
        g["image_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "provenance"):
            validate(t, e, g)

    def test_reject_forged_instruction(self):
        t, e, g = load()
        e["checks"]["indexed_data_offset"]["actual_bytes"] = "0000000000"
        with self.assertRaisesRegex(ValueError, "instruction anchor"):
            validate(t, e, g)

    def test_reject_inferred_edge_claimed_verified(self):
        t, e, g = load()
        t["routes"][0]["verified_edges"].append(
            ["0xffffffff999e4a20", "0xffffffff99884950"])
        with self.assertRaisesRegex(ValueError, "inferred edge"):
            validate(t, e, g)

    def test_reject_absent_primary_references(self):
        t, e, g = load()
        t["routes"][1]["references"] = []
        with self.assertRaisesRegex(ValueError, "citations"):
            validate(t, e, g)


if __name__ == "__main__":
    unittest.main()
