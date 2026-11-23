# PS4 cryptographic research inventory (non-secret metadata)

**Status:** Research index, 2026-11-23. **Target:** PS4 firmware 9.00, with historical and peripheral families for comparison.

This document is an index of publicly named cryptographic material, **not** a key dump or recovered-key database. A keyslot ID, ciphertext blob, derived symmetric key, and asymmetric private signing key are different objects. Documented naming does not establish that a value can be read from a PS4 host process or an EDK II payload.

## Evidence classes

- **DOCUMENTED_NAME:** A named family appears in the linked public documentation.
- **PUBLISHED_VALUE:** Some values have been published, but version/device applicability must be independently established. No values are included here.
- **HARDWARE_OBSERVED:** A narrowly scoped behavior, with captured hardware evidence, not inferred from a symbol.
- **REFERENCE_ONLY:** A binary analysis of a non-identical reference image.
- **UNKNOWN_EXPORTABILITY:** No verified software interface to export the secret.
- **COMMUNITY_SPECULATION:** An unverified public claim, never a basis for implementation.

## Key-family index

| Domain | Examples documented in public references | Limitations |
|---|---|---|
| SAMU master slots | KCA, KGA, KRA | Slot names are not exported raw key values |
| SAMU boot/flash slots | Sflash XTS, IPL over-encryption, IPL metadata and body | No demonstrated software export path |
| SAMU portability and protected services | pfsSKKey, sealedkey, portability keys, SBL services | Host API visibility differs from protected-key access |
| Content protection | RIF/HMAC, NPDRM, PFS | Historical findings are firmware-specific |
| EAP | EAP KBL, EAP kernel, EAP HDD | Separate from SAMU HDD material |
| EMC / Syscon | EMC IPL; Syscon patch and full firmware | Processing encrypted images is not proof of every console secret's availability |
| Storage | Console HDD, SAMU HDD, EAP HDD, extended USB storage | Distinct security/ownership domains |
| Firmware signing and packages | SELF, SAMU IPL, PKG metadata RSA, EKPFS | Private signing keys may not reside on the console |
| Kernel and system services | Crashdump, keystone, system-service encryption | Names do not prove raw-key export |
| AMD SMU | Firmware HMAC | Different processor from SAMU |
| Peripherals | DualShock 4, PSVR firmware and partition keys | Peripheral-specific; do not assume availability in host kernel |

The machine-readable companion inventory is [`research/crypto/key_families.json`](../research/crypto/key_families.json); it includes names, source URLs, and explicit **UNKNOWN_EXPORTABILITY** status, no key bytes or console-unique identifiers.

## Public references

- [PS4 Developer Wiki — Keys](https://www.psdevwiki.com/ps4/Keys)
- [PS4 Developer Wiki — KeySlots](https://www.psdevwiki.com/ps4/KeySlots)
- [PS4 Developer Wiki — AMD SAMU](https://www.psdevwiki.com/ps4/AMD_SAMU)
- [Al-Azif/ps4-cfw-toolkit](https://github.com/Al-Azif/ps4-cfw-toolkit). Its README differentiates EAP/EMC/Syscon firmware processing from SAMU IPL/SELF private signing keys; it states those private keys are **not on the console**.

## Software-only / no external SFlash reader

No external SFlash reader is needed to catalogue public names, firmware build/version, loaded-module metadata, verified non-secret service status and logged errors. That does **not** imply protected SAMU master slots, per-console key material or private signing keys can be dumped by software.

A future capability-reporting diagnostic may inventory hardware identity, discovered service names, verified non-secret status/error responses, and bounded JSONL telemetry. It must not export or record protected cryptographic values, private signing material, credentials, arbitrary secure-memory contents or encrypted console-unique secrets. Hardware operations require independently verified firmware-specific ABI definitions.

## Existing evidence boundary

The public main branch currently identifies the 9.00 native directboot-to-UEFI Shell pair under `prebuilt/hw-verified/` as hardware-proven. Later I/O/HAL work is not automatically hardware-proven. Separately supplied diagnostic evidence reports a 29-event ring-0 capture and a Ghidra *same-build reference* with 19,552 discovered functions; these are not a verified byte-for-byte map of the unavailable authoritative post-ReSys kernel dump.

## Community speculation is not technical evidence

The [November 2026 r/ps4homebrew thread](https://www.reddit.com/r/ps4homebrew/comments/1wsvq9r/unpopular_opinion_security_researchers_definitely/) speculates about private higher-firmware kernel/hardware exploits and BootROM-key discoveries. It does not demonstrate any such exploit, disclose protected material, or establish a SAMU capability. Label these claims **COMMUNITY_SPECULATION**.

## Repository hygiene

Do not commit secret values, dumps, private signing material or console-unique identifiers. Maintain source URLs and firmware/version applicability for each family. Preserve the verified UEFI boot artifacts and separate immutable hardware evidence from proposals.
