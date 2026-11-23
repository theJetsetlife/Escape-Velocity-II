# Historical CCP C source: native object build audit (CachyOS)

This is a **compilation and ABI diagnostic**, not a runnable SAMU exploit, key dumper or UEFI driver. The input source stays local. No raw firmware or protected key material is added to GitHub.

## Why the original C fragment does not directly build to a PS4 .bin

The flatz `ccp_crypto_bug.c` gist exposes `dump_raw_keys` and `dump_gen_keys` as functions. It does not provide the payload's native entry point, the exact loader ABI, a linker script, or definitions for its allocator and `sceSbl*` service calls. A C file may compile cleanly into a relocatable `.o` without yielding a callable payload.

The provided `file.c` and `file_build.h` were checked with a standalone Clang compile. The code compiles using freestanding x86-64 Jaguar-compatible flags. It still has unresolved references to `alloc`, `dealloc`, `memcpy`, `memcmp`, `memset`, `sceSblDriverMapPages`, `sceSblDriverUnmapPages`, `sceSblServiceCrypt`, `sceSblKeymgrSetKey`, and `sceSblKeymgrClearKey`. Symbols missing from a relocatable object are normal; they become blocking when linking a standalone image without implementations.

## CachyOS requirements

```bash
sudo pacman -S --needed python clang binutils
```

## Reproduce the compile-only check

Preserve your source/header in a private folder. From the repository root:

```bash
python3 tools/samu_source_audit.py \
  --source /private/path/file.c \
  --header /private/path/file_build.h \
  --output-dir .build/samu-object-audit

python3 -m unittest discover -s tests -p 'test_samu_source_audit.py' -v
```

Generated artifacts: `.build/samu-object-audit/source-compilation-only.o` and `.build/samu-object-audit/build-audit.json`. The manifest records source/header/object SHA-256 hashes, the object format, compilation status, and unresolved symbol names. It contains no key bytes and does not claim to execute the source.

The tool uses `clang`, `nm`, and `readelf`; it **never invokes a linker**, produces no flat `.bin`, and performs no privileged service calls.

## Evidence and next status gate

- `file.c` is compilable, but neither its symbol declarations nor a successful compilation proves the PS4 9.00 `sceSbl*` ABI.
- `src/diag/Makefile` and `src/diag/diag.ld` demonstrate an existing separate **non-UEFI** flat-binary contract for a read-only diagnostic payload. That is not a valid automatic link target for the original key-recovery functions.
- The exact 9.00 x86 host kernel is now verified from the user-supplied original; this supports static host-side symbol and ABI analysis only.
- The 9.00 SAMU IPL images are encrypted and have not yielded a verified plaintext secure-kernel implementation. Historical vulnerable-firmware behavior cannot be assumed on 9.00.

A standalone executable with protected-key extraction requires integration outside the scope of this research branch and is not provided.
