"""Branch target validation against synthetic x86 rel32 instructions only."""
from __future__ import annotations
import struct
import unittest
from synthetic_elf import BASE, build
from verify_call_graph import verify_edges


class BranchGraphTests(unittest.TestCase):
    def setUp(self):
        self.data = bytearray(build())
        self.source = BASE + 0x200
        self.target = BASE + 0x220
        struct.pack_into('<Bi', self.data, 0x200, 0xe8,
                         self.target - (self.source + 5))
        self.graph = dict(edges=[{
            'from': hex(self.source), 'to': hex(self.target),
            'confidence': 'verified', 'basis': f'direct call at {self.source:#x}'
        }])

    def test_synthetic_call_target(self):
        result = verify_edges(self.data, self.graph)
        self.assertEqual(result['verified_edges'], 1)
        self.assertTrue(result['pass_all_direct'])

    def test_cannot_misreport_jmp_as_call(self):
        self.data[0x200] = 0xe9
        self.assertFalse(verify_edges(self.data, self.graph)['pass_all_direct'])

    def test_reject_misdirected_edge(self):
        self.graph['edges'][0]['to'] = hex(self.target + 1)
        self.assertFalse(verify_edges(self.data, self.graph)['pass_all_direct'])

    def test_inferred_edges_do_not_become_verified(self):
        self.graph['edges'][0]['confidence'] = 'inferred'
        result = verify_edges(self.data, self.graph)
        self.assertEqual(result['unverified_inferences'], 1)
        self.assertFalse(result['pass_all_direct'])


if __name__ == '__main__':
    unittest.main()
