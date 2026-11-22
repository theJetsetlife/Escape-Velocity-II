#!/usr/bin/env python3
"""Prepare and verify a *local* six-binary PS4 9.00 research corpus.

Reads caller-supplied images only. Does not communicate with a console or
attempt to decrypt, sign, or execute firmware. No external dependencies.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

NOR_LENGTH = 0x02000000
MAX_KERNEL = 128 << 20
KERNEL_SHA256 = '8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3'
NOR_SHA256 = 'ebe04d3a6b46e2ed960f8e809655b5a25ca7ca9f168b224e0179bcb6810a2b04'

# Absolute offsets within the user's 32 MiB flash image, not device MMIO.
SLOTS = {
    'sam_ipl_primary.bin': (0x00204000, 0x0003e000, '9e4be557aa2c65f981b5f43e958e7fce37f1ea1383887265181ad017367904ad'),
    'sam_ipl_alternate.bin': (0x00242000, 0x0003e000, 'c088af4a788530dd083ebc359d965b148c653ae045e8e05c9ddb112b77664f2f'),
    'coreos_primary.bin': (0x003c0000, 0x00cc0000, '6f7634efb3b6600a39033fce5acaf4f6f9ff49d0a71fa8c74ea5ee5226a695b0'),
    'coreos_alternate.bin': (0x01080000, 0x00cc0000, '6a1935aff60e77df4271434922698f93ab6568c310f2dbd872c13a5256a802cf'),
}
FILES = {'kernel_900.bin': (None, None, KERNEL_SHA256),
         'sflash0.bin': (None, NOR_LENGTH, NOR_SHA256), **SLOTS}
CHUNK = 1 << 20


class CorpusError(ValueError):
    pass


def _regular(path: Path, max_size: int) -> int:
    if path.is_symlink():
        raise CorpusError(f'Input is a symbolic link: {path}')
    try:
        st = path.stat()
    except FileNotFoundError as exc:
        raise CorpusError(f'Missing required binary: {path}') from exc
    if not stat.S_ISREG(st.st_mode) or st.st_size > max_size or st.st_size == 0:
        raise CorpusError(f'Not a nonempty regular file <= {max_size} bytes: {path}')
    return st.st_size


def file_sha256(path: Path) -> tuple[str, int]:
    h = hashlib.sha256()
    size = _regular(path, MAX_KERNEL)
    with path.open('rb') as stream:
        while True:
            buf = stream.read(CHUNK)
            if not buf:
                break
            h.update(buf)
    return h.hexdigest(), size


def copy_atomic(src: Path, dest: Path) -> None:
    _regular(src, MAX_KERNEL)
    with tempfile.NamedTemporaryFile(prefix='.incoming-', dir=dest.parent, delete=False) as out:
        tmp = Path(out.name)
        try:
            with src.open('rb') as inp:
                shutil.copyfileobj(inp, out, CHUNK)
            out.flush()
            os.fsync(out.fileno())
        except BaseException:
            tmp.unlink(missing_ok=True)
            raise
    tmp.replace(dest)


def _slot_digest_in_nor(nor: Path, offset: int, size: int) -> str:
    h = hashlib.sha256()
    with nor.open('rb') as stream:
        stream.seek(offset)
        remaining = size
        while remaining:
            buf = stream.read(min(CHUNK, remaining))
            if not buf:
                raise CorpusError('NOR ended inside slot')
            h.update(buf)
            remaining -= len(buf)
    return h.hexdigest()


def verify(corpus: Path, strict: bool = True) -> dict:
    """Validate the six files, with cross-image NOR byte-equivalence checks."""
    if corpus.is_symlink() or not corpus.is_dir():
        raise CorpusError('Corpus must be a local directory, not a symlink')
    records: dict = {}
    for name, (_, expected_size, expected_sha) in FILES.items():
        path = corpus / name
        digest, size = file_sha256(path)
        size_ok = (size == expected_size) if expected_size is not None else (0 < size <= MAX_KERNEL)
        hash_ok = digest == expected_sha
        records[name] = dict(bytes=size, sha256=digest, matches_known_sample=hash_ok,
                             expected_size_ok=size_ok)
        if not size_ok:
            raise CorpusError(f'Incorrect length: {name} ({size})')
        if strict and not hash_ok:
            raise CorpusError(f'Known-sample SHA-256 mismatch: {name}')
    nor = corpus / 'sflash0.bin'
    for name, (offset, size, _) in SLOTS.items():
        slot_digest = _slot_digest_in_nor(nor, offset, size)
        records[name]['nor_offset'] = hex(offset)
        records[name]['matches_nor_extract'] = (slot_digest == records[name]['sha256'])
        if not records[name]['matches_nor_extract']:
            raise CorpusError(f'Extract differs from NOR at {offset:#x}: {name}')
    return dict(schema='evii-samu-real-corpus-v1', kind='user-supplied-binary-corpus',
                strict_known_sample=strict, records=records, pass_all=True)


def init(kernel: Path, nor: Path, corpus: Path, strict: bool = True) -> dict:
    if corpus.is_symlink():
        raise CorpusError('Refusing symlink corpus')
    kernel_sha, kernel_size = file_sha256(kernel)
    nor_sha, nor_size = file_sha256(nor)
    if nor_size != NOR_LENGTH:
        raise CorpusError(f'NOR length {nor_size} != {NOR_LENGTH}')
    if strict and (kernel_sha != KERNEL_SHA256 or nor_sha != NOR_SHA256):
        raise CorpusError('Uploaded kernel/NOR differs from pinned 9.00 samples')
    corpus.mkdir(parents=True, exist_ok=True, mode=0o700)
    # A local-only corpus is expected to be excluded from any Git checkout.
    copy_atomic(kernel, corpus / 'kernel_900.bin')
    copy_atomic(nor, corpus / 'sflash0.bin')
    with (corpus / 'sflash0.bin').open('rb') as src:
        for name, (offset, size, _) in SLOTS.items():
            with tempfile.NamedTemporaryFile(prefix='.slot-', dir=corpus, delete=False) as stream:
                tmp = Path(stream.name)
                try:
                    src.seek(offset)
                    remaining = size
                    while remaining:
                        buf = src.read(min(CHUNK, remaining))
                        if not buf:
                            raise CorpusError(f'Truncated region: {name}')
                        stream.write(buf)
                        remaining -= len(buf)
                    stream.flush()
                    os.fsync(stream.fileno())
                except BaseException:
                    tmp.unlink(missing_ok=True)
                    raise
            tmp.replace(corpus / name)
    result = verify(corpus, strict=strict)
    dest = corpus / 'manifest.json'
    dest.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    try:
        os.chmod(dest, 0o600)
    except OSError:
        pass
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p_init = commands.add_parser('init', help='Import user-supplied kernel and NOR; extract four firmware slots')
    p_init.add_argument('--kernel', type=Path, required=True)
    p_init.add_argument('--nor', type=Path, required=True)
    for c in (p_init, commands.add_parser('verify', help='Hash and cross-check six existing binary inputs')):
        c.add_argument('--corpus', type=Path, default=Path('.local-corpus'))
        c.add_argument('--allow-other-samples', action='store_true', help='Keep structure/cross-extract verification, skip known SHA pinning')
    args = parser.parse_args()
    try:
        result = (init(args.kernel, args.nor, args.corpus, not args.allow_other_samples)
                  if args.command == 'init'
                  else verify(args.corpus, not args.allow_other_samples))
    except (CorpusError, OSError) as exc:
        parser.exit(2, f'FAIL: {exc}\n')
    print(f"PASS: verified {len(result['records'])} actual user-supplied binaries; "
          'all four secure partitions match NOR byte-for-byte')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
