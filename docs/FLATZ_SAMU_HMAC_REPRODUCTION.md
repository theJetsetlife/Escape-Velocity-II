# flatz's historical PS4 SAMU HMAC-length flaw: reproducible policy model

**Research date:** 2026-11-23. **Status:** Offline synthetic-key model, not a reimplementation of the 7.55 exploit payload and not a working key dump.

## Primary references

- flatz, [original `ccp_crypto_bug.c` proof of concept](https://gist.github.com/flatz/22215327864d7512e52268f9c9c51cd8), 2021-12-18.
- PS4 Developer Wiki, [Vulnerabilities — missing HMAC key length check](https://www.psdevwiki.com/ps4/Vulnerabilities).
- PS4 Developer Wiki, [AMD SAMU](https://www.psdevwiki.com/ps4/AMD_SAMU).

## Source-derived behavior

flatz's PoC contains `dump_raw_keys` and `dump_gen_keys` operating through kernel-provided SBL/CCP calls. The issue was a missing check for HMAC key lengths below 16 bytes. Historically, exposing MAC results computed under undersized prefixes enabled partial keyring recovery. The wiki says firmware 7.55 was vulnerable and firmware 9.00 was patched; it also says master keyrings 0–2 were inaccessible using that vulnerability because they were locked during boot.

The original PoC uses privileged interfaces (`sceSblDriverMapPages`, `sceSblServiceCrypt`, and key-manager methods) that are **not** reimplemented here. An existing GCC object with unresolved references to those functions does not establish a valid target ABI or a usable firmware 9.00 exploitation path.

## Reproduction

Run from the repository root:

```shell
python3 tools/samu_hmac_policy.py
python3 -m unittest discover -s tests -p 'test_samu_hmac_policy.py' -v
```

The model demonstrates a legacy policy that accepts a one-byte HMAC key length versus a hardened policy that rejects any length below 16. It uses a fixed, public 32-byte fixture and reports **only acceptance/rejection**, not key bytes or digests. Its results do not establish that the real CCP hardware implements this exact model on any firmware version.

## Scope and limitations

- No console I/O, memory mapping, ioctl, direct kernel calls, keyslot enumeration, brute-force routines, key derivation from console identifiers, or USB secret export.
- No claim of successful protected-key recovery on PS4 firmware 9.00.
- No changes to native directboot, EDK II, or hardware-verified prebuilt artifacts.
- Next defensible analysis step: compare the actual firmware 9.00 secure service's key-length validation with the historical writeup, using static analysis and documented evidence provenance. A same-build reference kernel is not byte-identical to a post-ReSys dump.
