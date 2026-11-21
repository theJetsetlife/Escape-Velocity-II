# Extra kernel import tables (OpenGoldHEN ksdk_offsets format: 77 little-endian u32, kernel.h enum order)

Used by `mkfwident.py` next to OpenGoldHEN's own `assets/tables`. Data only; nothing here is executed.

| File | Version | Origin | Cross-check |
|---|---|---|---|
| `ksdk_offsets_1400.bin` (sha256 `6567a5e26949b50742d790a03282cb7aee172ab163546ce350a8c5411f7b7c5f`) | 14.00 | 308 bytes at offset 0x11740 of `BDMV/JAR/00000.JAR!/bonk/hen.bin` (sha256 `92d99992d3720ed3c6a9b0d9405ba61a11892923eada67ea6ce3fe2c83934e48`) in `GoldHEN-PS4-14.00-BDJ-Experimental-POYGAMING.iso` (sha256 `e06134d40f65d4ad8b3c8aee36c63c9909267e6b9476b4564794179efd2f532a`), an unofficial experimental GoldHEN 14.00 BD-J build (its NOTICE.TXT: "Not an official SiSTRo release") | XFAST_SYSCALL = 0x1c0; KMEM_ALLOC 0x466540, KERN_REBOOT 0x3a2850, KERNEL_MAP 0x22d1d50 equal the independent 14.00 row of fwtable.h (PS4 Linux loader) |

mkfwident.py refuses an extra table whose KMEM_ALLOC / KERN_REBOOT / KERNEL_MAP disagree with fwtable.h for the same version.
