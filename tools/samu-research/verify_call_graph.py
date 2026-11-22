#!/usr/bin/env python3
"""Validate direct x86 rel32 call/jmp edges in an offline kernel ELF.

This does NOT infer control flow: only previously identified, cited direct
branches get verified. Other call-graph edges remain explicitly unconfirmed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from verify_kernel_900 import InvalidKernel, load_kernel, parse_elf, virtual_to_file

BRANCH_AT = re.compile(r'\b(call|jump) at (?:VA )?(0x[0-9a-f]+)', re.I)


def verify_edges(data: bytes, graph: dict) -> dict:
    elf = parse_elf(data)
    report: list[dict] = []
    for edge in graph['edges']:
        item = {'from': edge['from'], 'to': edge['to'],
                'confidence': edge['confidence'], 'verified': False}
        if edge['confidence'] != 'verified':
            item['status'] = 'UNVERIFIED_INFERENCE'
            report.append(item)
            continue
        m = BRANCH_AT.search(edge.get('basis', ''))
        if m is None:
            item['status'] = 'MISSING_DIRECT_BRANCH_EVIDENCE'
            report.append(item)
            continue
        kind, callsite = m.groups()
        site, target = int(callsite, 16), int(edge['to'], 16)
        try:
            offset, index = virtual_to_file(elf['segments'], site, 5)
            raw = data[offset:offset + 5]
            opcode = 0xe8 if kind.lower() == 'call' else 0xe9
            displacement = struct.unpack_from('<i', raw, 1)[0]
            destination = (site + 5 + displacement) & ((1 << 64) - 1)
            virtual_to_file(elf['segments'], destination, 1)
            item['verified'] = raw[0] == opcode and destination == target
            item['status'] = 'PASS' if item['verified'] else 'MISMATCH'
            item['callsite'] = hex(site)
            item['computed_target'] = hex(destination)
            item['segment'] = index
        except (InvalidKernel, struct.error, ValueError) as exc:
            item['status'] = 'INVALID_ELF_MAPPING'
            item['error'] = str(exc)
        report.append(item)
    verified_count = sum(r['verified'] for r in report)
    required_count = sum(e['confidence'] == 'verified' for e in graph['edges'])
    return {'verified_edges': verified_count,
            'direct_edges': required_count,
            'unverified_inferences': len(graph['edges']) - required_count,
            'pass_all_direct': verified_count == required_count and required_count > 0,
            'edges': report}


def run(kernel: str | Path, graph_path: str | Path) -> dict:
    data = load_kernel(kernel)
    graph = json.loads(Path(graph_path).read_text(encoding='utf-8'))
    actual_hash = hashlib.sha256(data).hexdigest()
    if actual_hash != graph['image_sha256']:
        raise InvalidKernel('Kernel SHA-256 does not match call-graph provenance')
    r = verify_edges(data, graph)
    r['kernel_sha256_match'] = True
    return r


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kernel', help='Authorized, local-only kernel dump')
    ap.add_argument('--graph', default=str(Path(__file__).with_name('call_graph.json')))
    ap.add_argument('--out', help='Optional JSON output, never contains image bytes')
    args = ap.parse_args()
    try:
        result = run(args.kernel, args.graph)
    except (InvalidKernel, OSError, KeyError, ValueError, struct.error) as exc:
        ap.exit(2, f'INVALID: {exc}\n')
    if args.out:
        Path(args.out).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(('PASS' if result['pass_all_direct'] else 'FAIL'),
          f"{result['verified_edges']}/{result['direct_edges']} direct branch edges",
          f"{result['unverified_inferences']} intentionally unresolved")
    if not result['pass_all_direct']:
        raise SystemExit(1)
