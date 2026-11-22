#!/usr/bin/env python3
"""Read-only, segment-aware verifier for ONE known PS4 9.00 kernel capture.

It verifies the ELF identity, structural validity of the program headers,
SHA-256, and pinned instruction bytes in executable PT_LOAD extents.
It does not execute the image or read protected or device memory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import struct
from pathlib import Path

HASH = '8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3'
EXPECTED_BASE = 0xffffffff993c8000
EXPECTED_ENTRY = 0xffffffff996393c0
ELF_HEADER = struct.Struct('<16sHHIQQQIHHHHHH')
PROGRAM_HEADER = struct.Struct('<IIQQQQQQ')
PT_LOAD = 1
PF_X = 1
MAX_FILE_SIZE = 128 << 20  # 128 MiB cap: no unbounded reads, devices, or pipes

# (virtual address, bytes from original instruction boundary)
CHECKS = {
 'sender_prologue': (0xffffffff999e4ee0, '554889e5'),
 'sender_max_4096': (0xffffffff999e4eed, '81fe00100000'),
 'sender_min_24': (0xffffffff999e4ef5, '83f917'),
 'message_copy_offset': (0xffffffff999e4f53, '4801df'),
 'message_zero_qword': (0xffffffff999e4f62, '48c7801010000000000000'),
 'no_copyback': (0xffffffff999e4ed4, '31d2'),
 'yes_copyback': (0xffffffff999e4fd4, 'ba01000000'),
 'service_size_max': (0xffffffff999f9074, '81fea9000000'),
 'init_allocation_1MiB': (0xffffffff999e4349, 'bf00001000'),
 'init_alignment': (0xffffffff999e4378, 'a9ff3f0000'),
 'indexed_selector_offset': (0xffffffff99884979, 'ba00200200'),
 'indexed_data_offset': (0xffffffff99884997, 'ba04200200'),
 'submit_index_34': (0xffffffff999e4a2d, 'bf34000000'),
 'submit_index_33': (0xffffffff999e4a3d, 'bf33000000'),
 'submit_start_index_32': (0xffffffff999e4a49, 'bf32000000'),
 'submit_status_index_4a': (0xffffffff999e4a58, 'bf4a000000'),
 'dispatch_13_entries': (0xffffffff999e45c1, '833f0d'),
 'dispatch_callback_call': (0xffffffff999e464e, '41ffd5'),
 'service_to_sender': (0xffffffff999f9197, 'e834bdfeff'),
}


class InvalidKernel(ValueError):
    """The image is not structurally valid for this verification."""


def _range_within(start: int, size: int, bound: int) -> bool:
    return 0 <= start <= bound and 0 <= size <= bound - start


def parse_elf(data: bytes) -> dict:
    if len(data) < ELF_HEADER.size:
        raise InvalidKernel('Truncated ELF header')
    (ident, kind, machine, version, entry, phoff, shoff, flags,
     ehsize, phentsize, phnum, shentsize, shnum, shstrndx) = ELF_HEADER.unpack_from(data)
    if ident[:4] != b'\x7fELF' or ident[4:7] != b'\x02\x01\x01':
        raise InvalidKernel('Expected little-endian ELF64')
    if ident[7] != 9 or kind != 2 or machine != 62 or version != 1:
        raise InvalidKernel('Unexpected ELF ABI, type, architecture or version')
    if ehsize != ELF_HEADER.size or phentsize != PROGRAM_HEADER.size:
        raise InvalidKernel('Unexpected ELF header or program header size')
    if phnum == 0 or phnum == 0xffff:
        raise InvalidKernel('Missing or extended ELF program headers')
    if phoff < ehsize:
        raise InvalidKernel('Program headers overlap the ELF header')
    if not _range_within(phoff, phentsize * phnum, len(data)):
        raise InvalidKernel('Program-header table extends outside file')
    segments = []
    for i in range(phnum):
        (p_type, p_flags, p_offset, p_vaddr, p_paddr,
         p_filesz, p_memsz, p_align) = PROGRAM_HEADER.unpack_from(data, phoff + i * phentsize)
        if p_filesz > p_memsz:
            raise InvalidKernel(f'Program header {i}: filesz exceeds memsz')
        if p_align not in (0, 1) and p_align & (p_align - 1):
            raise InvalidKernel(f'Program header {i}: invalid power-of-two alignment')
        if not _range_within(p_offset, p_filesz, len(data)):
            raise InvalidKernel(f'Program header {i}: file range outside input')
        if p_vaddr + p_memsz > (1 << 64):
            raise InvalidKernel(f'Program header {i}: virtual address overflow')
        segments.append(dict(index=i, type=p_type, flags=p_flags,
                             offset=p_offset, vaddr=p_vaddr, filesz=p_filesz,
                             memsz=p_memsz, align=p_align))
    return dict(entry=entry, phoff=phoff, phentsize=phentsize,
                phnum=phnum, segments=segments)


def virtual_to_file(segments: list[dict], va: int, length: int) -> tuple[int, int]:
    """Resolve VA only through an executable, file-backed ELF PT_LOAD."""
    if length <= 0 or va < 0 or va >= (1 << 64) or length > (1 << 64) - va:
        raise InvalidKernel('Invalid virtual address extent')
    matches = [s for s in segments if
               s['type'] == PT_LOAD and s['flags'] & PF_X and
               va >= s['vaddr'] and
               length <= s['filesz'] and va - s['vaddr'] <= s['filesz'] - length]
    if len(matches) != 1:
        raise InvalidKernel(f'Expected exactly one executable file mapping for {va:#x}, found {len(matches)}')
    s = matches[0]
    return s['offset'] + (va - s['vaddr']), s['index']


def load_kernel(path: str | Path) -> bytes:
    """Read a regular, bounded file; reject symlinks, FIFOs and device nodes.

    This intentionally never touches physical memory or a live device. The
    caller's kernel image is never copied into this project's output tree.
    """
    p = Path(path)
    if p.is_symlink():
        raise InvalidKernel('Refusing a symbolic-link input')
    with p.open('rb') as fh:
        metadata = os.fstat(fh.fileno())
        if not stat.S_ISREG(metadata.st_mode):
            raise InvalidKernel('Input must be a regular file')
        if metadata.st_size > MAX_FILE_SIZE:
            raise InvalidKernel('Input exceeds maximum supported verifier file size')
        data = fh.read(MAX_FILE_SIZE + 1)
        if len(data) > MAX_FILE_SIZE:
            raise InvalidKernel('Input grew beyond maximum supported size')
        if len(data) != metadata.st_size:
            raise InvalidKernel('Input size changed during acquisition')
    return data


def run(path: str | Path) -> dict:
    data = load_kernel(path)
    elf = parse_elf(data)
    sha = hashlib.sha256(data).hexdigest()
    rx = [s for s in elf['segments'] if s['type'] == PT_LOAD and s['flags'] & PF_X]
    base_matches = any(s['vaddr'] == EXPECTED_BASE for s in rx)
    entry_matches = elf['entry'] == EXPECTED_ENTRY
    try:
        virtual_to_file(elf['segments'], elf['entry'], 1)
        entry_executable = True
    except InvalidKernel:
        entry_executable = False
    results = {}
    for label, (va, hx) in CHECKS.items():
        expected = bytes.fromhex(hx)
        try:
            offset, segment = virtual_to_file(elf['segments'], va, len(expected))
            actual = data[offset:offset + len(expected)]
            results[label] = dict(runtime_address=hex(va), file_offset=hex(offset),
                                  segment=segment, verified=actual == expected,
                                  actual_bytes=actual.hex())
        except InvalidKernel as exc:
            results[label] = dict(runtime_address=hex(va), verified=False, error=str(exc))
    passed = sum(row['verified'] for row in results.values())
    report = dict(input_size=len(data), sha256=sha, sha256_match=sha == HASH,
                  elf_entry=hex(elf['entry']), expected_entry_match=entry_matches,
                  expected_base_match=base_matches, entry_executable=entry_executable,
                  program_header_count=elf['phnum'],
                  elf_segments=[{**s, 'vaddr': hex(s['vaddr'])} for s in elf['segments']],
                  checks=results, passed=passed, total=len(results))
    report['pass_all'] = all((report['sha256_match'], report['expected_entry_match'],
                              report['expected_base_match'], report['entry_executable'],
                              passed == len(results)))
    return report


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('kernel', help='Local kernel image, never uploaded or modified')
    cli.add_argument('--out', help='Optional JSON report output path')
    args = cli.parse_args()
    try:
        result = run(args.kernel)
    except (InvalidKernel, OSError, struct.error) as exc:
        cli.exit(2, f'INVALID: {exc}\n')
    if args.out:
        Path(args.out).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(('PASS' if result['pass_all'] else 'FAIL'),
          f"{result['passed']}/{result['total']} executable segment checks",
          f"phdrs={result['program_header_count']}",
          'sha256_ok' if result['sha256_match'] else 'sha256_MISMATCH')
    if not result['pass_all']:
        raise SystemExit(1)
