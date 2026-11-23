#!/usr/bin/env python3
"""Compile a user-supplied PS4 C fragment to an object and report its link state.

This tool intentionally DOES NOT link a runnable payload, resolve privileged
service functions, execute input source, or export any cryptographic material.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd):
    return subprocess.run(cmd, check=True, text=True, capture_output=True).stdout


def get_undefined_symbols(object_path):
    raw = run(["nm", "-u", str(object_path)])
    names = []
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] in {"U", "w", "W"}:
            names.append(parts[-1])
    return sorted(set(names))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="Local C fragment to compile; not committed")
    parser.add_argument("--header", type=Path, required=True,
                        help="Local compatibility header")
    parser.add_argument("--output-dir", type=Path,
                        default=Path(".build/samu-object-audit"))
    args = parser.parse_args(argv)

    source, header = args.source.resolve(), args.header.resolve()
    for value, extension in ((source, ".c"), (header, ".h")):
        if not value.is_file() or value.suffix != extension:
            parser.error(f"expected a readable {extension} file: {value}")
    for utility in ("clang", "nm", "readelf"):
        if not shutil.which(utility):
            parser.error(f"missing command: {utility}")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    obj = output_dir / "source-compilation-only.o"
    flags = [
        "-std=gnu11", "-O2", "-march=btver2", "-ffreestanding", "-fno-builtin",
        "-fno-stack-protector", "-mno-red-zone", "-mgeneral-regs-only",
        "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-Wall",
        "-Wextra", "-Werror",
    ]
    cmd = ["clang", *flags, "-include", str(header), "-c", str(source), "-o", str(obj)]
    try:
        run(cmd)
        metadata = run(["readelf", "-h", str(obj)])
    except subprocess.CalledProcessError as exc:
        print(exc.stderr or exc.stdout, file=sys.stderr)
        return exc.returncode
    if "REL (Relocatable file)" not in metadata or "X86-64" not in metadata:
        print("unexpected output: object must be ELF64 relocatable x86-64",
              file=sys.stderr)
        return 1
    unlinked = get_undefined_symbols(obj)
    report = {
        "schema": "evii.ps4-source-object-audit.v1",
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
        "object_path": str(obj),
        "object_format": "ELF64 x86-64 relocatable",
        "compile_passed": True,
        "link_performed": False,
        "runnable_ps4_payload": False,
        "undefined_symbols": unlinked,
        "note": "This output is a non-executable object. No privileged services are resolved or called.",
    }
    manifest = output_dir / "build-audit.json"
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
