# PS4 firmware 9.00: supplied binary evidence recheck

**Recorded:** 2026-11-23. **Method:** Local static reads of already-supplied private images; no PS4 hardware interaction. **Track:** Native PS4 diagnostics, no EDK II dependency.

## Source image identities

| Image | Size | SHA-256 | Classification |
|---|---:|---|---|
| `kernel(1).bin` | 44,040,192 | `8fa5250621bf30bdb21730108f5b35c103bc898def60cf91417f0237cd3be0b3` | Exact post-ReSys 9.00 host x86 kernel; available |
| `sflash0.bin` | 33,554,432 | `ebe04d3a6b46e2ed960f8e809655b5a25ca7ca9f168b224e0179bcb6810a2b04` | Full user-supplied NOR |
| `sam_ipl_primary.bin` | 253,952 | `9e4be557aa2c65f981b5f43e958e7fce37f1ea1383887265181ad017367904ad` | Matches NOR carve byte-for-byte |
| `sam_ipl_alternate.bin` | 253,952 | `c088af4a788530dd083ebc359d965b148c653ae045e8e05c9ddb112b77664f2f` | Matches NOR carve byte-for-byte |

The separately generated kernel symbol recovery files had stated that the **exact** post-ReSys image was absent *from that prior generation run*. That statement was not a permanent limitation: the original exact SHA-matching image is available in the user's existing file library. Do not confuse unavailable in one processing run with not supplied.

The independently loaded kernel identifies as ELF64 FreeBSD x86-64, no section header, with six program headers. The previously archived local verifier's 19 instruction-byte checks were rerun against the exact bytes: **19/19 passed**. The five previously labeled verified rel32 direct call/jump edges were also independently recomputed from the exact bytes: **5/5 passed**. Three edges remain hypotheses, not verified calls.

These checks substantiate the selected **x86 SAMU host-driver** assembly observations, not arbitrary service authorization, keyslot contents, firmware decryption, or SAMU secure-kernel behavior.

## SAMU firmware container observations

Both SAMU IPL slots were independently carved from the NOR and compared byte-for-byte with the supplied standalone files. Both begin with opaque bytes rather than a plain ELF header. Their measured Shannon byte entropies were **7.9992** and **7.9993 bits/byte**, respectively (maximum 8). This is consistent with encryption or compression but entropy alone does not prove a particular cipher, key, or secure-kernel patch level.

No decrypted SAMU secure-kernel instruction stream has been established from these images.

## Patch claim versus local evidence

[PSDevWiki](https://www.psdevwiki.com/ps4/Vulnerabilities) reports the historical flatz short-HMAC-key vulnerability as unpatched on 7.55 and patched by 9.00. The separately documented [original flatz PoC](https://gist.github.com/flatz/22215327864d7512e52268f9c9c51cd8) establishes historical source behavior. Neither a vulnerable-7.55 SAMU secure-kernel comparison nor a decrypted-9.00 SAMU secure-kernel diff is present in this validated corpus.

**Result:** The exact 9.00 *host-side* message path can be studied from first-hand binary evidence. The specific claim that the SAMU *secure-side* short-key-length validation was fixed on 9.00 remains **reported, not proven by these host/NOR bytes alone**. Do not assert that the exploit still works based on absence of a verified secure-side diff.

## Source handling

The original images remain private. No raw firmware, derived console identifiers, key bytes, or sensitive memory offsets are committed. Reproducible evidence is restricted to image identities and non-sensitive validation summaries. This is an analysis note and makes no claim of live PS4 testing.
