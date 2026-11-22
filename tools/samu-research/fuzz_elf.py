#!/usr/bin/env python3
"""Deterministic, offline, property-style ELF parser mutator.

No kernel samples required; all bytes originate in synthetic_elf.py.
No memory access, networking, devices, Docker privilege or SAMU commands.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import random
from synthetic_elf import build, BASE
from verify_kernel_900 import InvalidKernel, parse_elf, virtual_to_file


def mutation(rng: random.Random, baseline: bytes) -> bytes:
    data = bytearray(baseline)
    operation = rng.randrange(7)
    if operation == 0:
        data = data[:rng.randrange(len(data) + 1)]
    elif operation == 1:
        for _ in range(rng.randrange(1, 17)):
            i = rng.randrange(len(data)); data[i] ^= rng.randrange(1, 256)
    elif operation == 2:
        i = rng.randrange(len(data)); data[i] = rng.randrange(256)
    elif operation == 3:
        i = rng.randrange(len(data)); data[i:i] = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 33)))
    elif operation == 4:
        i = rng.randrange(len(data)); del data[i:i+rng.randrange(1, 33)]
    elif operation == 5:
        i = rng.choice([4, 5, 7, 16, 18, 32, 42, 54, 56, 64, 72, 96, 120, 144]);
        if i < len(data): data[i] ^= 0xff
    else:
        i = rng.randrange(len(data)); size = min(rng.randrange(1, 9), len(data) - i)
        data[i:i + size] = bytes(rng.randrange(256) for _ in range(size))
    return bytes(data)


def run(iterations: int, seed: int) -> dict:
    if not 0 <= iterations <= 500000:
        raise ValueError('iterations outside safe range [0,500000]')
    baseline = build()
    parsed = parse_elf(baseline)
    assert virtual_to_file(parsed['segments'], BASE + 0x200, 1) == (0x200, 0)
    rng = random.Random(seed)
    accepted = 0
    rejected = 0
    for _ in range(iterations):
        item = mutation(rng, baseline)
        try:
            parsed = parse_elf(item)
            # Crucial property: any mapped result stays inside the input.
            for va in (BASE + 0x200, BASE + 0x2ff, BASE + 0x300,
                       BASE + 0x800, BASE + 0x8ff, BASE + 0x1800):
                for length in (0, 1, 2, 16, 128, 1024):
                    try:
                        offset, index = virtual_to_file(parsed['segments'], va, length)
                    except InvalidKernel:
                        continue
                    assert length > 0
                    assert 0 <= index < parsed['phnum']
                    assert 0 <= offset <= len(item) - length
            accepted += 1
        except InvalidKernel:
            rejected += 1
    return dict(seed=seed, iterations=iterations, accepted=accepted,
                rejected=rejected, fixture_sha256=hashlib.sha256(baseline).hexdigest(),
                status='PASS')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--iterations', type=int, default=5000)
    ap.add_argument('--seed', type=int, default=900)
    args = ap.parse_args()
    print(json.dumps(run(args.iterations, args.seed), sort_keys=True))
