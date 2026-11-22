#!/usr/bin/env python3
"""Fail-closed guard for a host-only security research subtree.

Checks only the new SAMU research source/docs and its CI configuration; it
never recurses into proprietary `reference/` or `prebuilt/` repository areas.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import stat

REJECT_SUFFIXES = {'.bin', '.elf', '.efi', '.pup', '.pem', '.p12',
                   '.key', '.pfx', '.sqlite', '.db', '.zip', '.tar', '.xz', '.kdb'}
TEXT_SUFFIXES = {'.py', '.c', '.h', '.json', '.csv', '.md', '.yml', '.yaml', ''}
FORBIDDEN = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
MAX_SOURCE_SIZE = 1_000_000
BANNED_CODE_CALLS = re.compile(
    rb'(?m)^\s*(?:import|from)\s+(?:socket|requests|urllib|http|ftplib|paramiko|subprocess)\b'
    rb'|\b(?:iopl|ioperm|ptrace|popen|system|fork)\s*\('
    rb'|[\"\']/dev/(?:mem|port|kmem)[\"\']')


def inspect_path(p: Path) -> list[str]:
    issues: list[str] = []
    if p.is_symlink():
        return [f'{p}: symbolic links are forbidden in the clean-room module']
    if not p.is_file():
        return [f'{p}: unexpected non-regular entry']
    if p.suffix.lower() in REJECT_SUFFIXES or (p.suffix.lower() not in TEXT_SUFFIXES and p.name != 'Dockerfile.cleanroom'):
        issues.append(f'{p}: unapproved or binary extension')
    if p.stat().st_size > MAX_SOURCE_SIZE:
        issues.append(f'{p}: source exceeds 1 MiB')
        return issues
    data = p.read_bytes()
    if b'\x00' in data:
        issues.append(f'{p}: NUL byte / possible binary content')
    if FORBIDDEN.search(data):
        issues.append(f'{p}: embedded private-key block')
    if p.suffix.lower() in {'.c', '.h', '.py'} and BANNED_CODE_CALLS.search(data):
        issues.append(f'{p}: networking, live-memory, or process-execution primitive')
    try:
        data.decode('utf-8')
    except UnicodeDecodeError:
        issues.append(f'{p}: non-UTF-8 source content')
    return issues


def guard(paths: list[Path]) -> dict:
    findings: list[str] = []
    checked: list[str] = []
    for root in paths:
        if root.is_symlink():
            findings.append(f'{root}: symlink root refused')
            continue
        if not root.exists():
            findings.append(f'{root}: required source missing')
            continue
        entries = sorted(root.rglob('*') if root.is_dir() else [root])
        for p in entries:
            if p.is_dir() and not p.is_symlink():
                continue
            if any(part in {'.build', '.local-corpus', '__pycache__', '.git'} for part in p.parts):
                continue
            checked.append(str(p))
            findings.extend(inspect_path(p))
    return dict(status='PASS' if not findings else 'FAIL',
                files_checked=len(checked), findings=findings)


def default_paths() -> list[Path]:
    root = Path(__file__).resolve().parents[2]
    return [root / 'tools/samu-research',
            root / 'docs/SAMU_900_CLEANROOM_LAB.md',
            root / 'docs/SAMU_900_SECURITY_AUDIT.md',
            root / 'docs/SAMU_900_REA_WORKFLOW.md',
            root / '.github/workflows/samu-cleanroom.yml']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path)
    args = parser.parse_args()
    r = guard(args.paths if args.paths else default_paths())
    print(json.dumps(r, sort_keys=True))
    if r['status'] != 'PASS':
        raise SystemExit(1)
