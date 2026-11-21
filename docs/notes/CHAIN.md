# Delivery chain — PS4 Fat 9.00 → diag.bin (verified step by step)

Sources checked: Kit.zip (WebKitty-main from GitHub), Open.zip (OpenGoldHEN-main),
latest_ps4_sdk.zip (SDK 12.508 core: `ref/sdk-12508/core/sdk/target/include`).

| # | step | evidence |
|---|---|---|
| 1 | PS4 browser opens `http://10.0.0.71:9898/` | docker `webkitty`: plain `python -m http.server` over the WebKitty folder (read-only) |
| 2 | Site = WebKitty exactly as GitHub, except the 7 `goldhen_v*.bin` | `diff -rq` vs Kit.zip: only those 7 differ; local HEAD `ca35118` == GitHub HEAD; originals in `webkitty-backup/goldhen-original/` (== Kit) |
| 3 | First visit: `cache.js` → `cache.html` → iframe `includes/caches/cache9xx.html` (`manifest/9xx.manifest`) → back to index | `includes/js/cache.js`, `cache.html`. Requires `.manifest` served as `text/cache-manifest` (GitHub Pages does this) or it loops forever; all 138 CACHE entries return 200 |
| 4 | Jailbreak, fw 9.00 → default chain **PSFree Lapse Bundle** (`src/psfree-lapse/bundle.js`) | `includes/js/firmware.js`: Bundle prefMin 8.00–prefMax 9.60; Modular prefMax 7.55 |
| 5 | Lapse kernel exploit, then 9.00 kpatch: sysent[11] = `jmp [rsi]` gadget, syscall-origin check removed | `src/psfree-lapse/kpatch/900.c` (LEDGER A1–A3, A8) |
| 6 | HEN = GoldHEN (default) → `payload_path = includes/payloads/HEN/goldhen_v2.4b18.14.bin` (or the saved `GHVer`; all 7 are diag.bin) | `includes/js/HENs.js` GoldHEN() |
| 7 | `kexSuccess` → `PayloadLoader(payload_path)`: fetch, mmap RWX (MAP_ANON\|MAP_PRIVATE), copy, `pthread_create(entry = buffer+0, arg = 0)` | bundle.js `PayloadLoader` (Modular chain: `lapse.mjs` runPayload, same entry, `call_void`) |
| 8 | Payload in userland → `syscall 11(fn, arg)` → ring0 `fn(td, uap)` | **real GoldHEN does the same**: goldhen_v2.4b18.14.bin entry `jmp 0x16ab` → if not already ring0: `syscall(0, 11, fn)`. diag.bin: direct `syscall` rax=11, rdi=ring0_main, rsi=ctx |
| 9 | diag.bin reports to `10.0.0.71:9031` (docker `diag-receiver`), saves `reports/diag-*.bin/.txt` | `src/diag/user.c`; ABI constants match SDK headers (below) |

## diag.bin vs the GoldHEN slot contract
- entry at offset 0, no argument needed, all callee-saved registers preserved, returns → thread exits.
- position independent, no relocations (build gate), no SSE, `.bss` inside the file.
- ring0 entered only after: connection to the PC works, CPUID shows no SMEP/SMAP, 9.00 kernel signatures match.

## ABI constants checked against SDK 12.508 headers
PROT_READ/WRITE/EXEC 1/2/4, MAP_PRIVATE 2, MAP_ANON 0x1000 (sys/mman.h); AF_INET 2, SOCK_STREAM 1,
SOL_SOCKET 0xffff (sys/socket.h); IPPROTO_TCP 6 (netinet/in.h); TCP_NODELAY 1 (netinet/tcp.h);
sockaddr_in = {u8 len, u8 family (sa_family_t=uint8), u16 port, in_addr, char zero[8]} = 16 B;
timespec = {int64 tv_sec, long tv_nsec}. Syscall numbers are not in the SDK (it goes through
libkernel); they come from WebKitty `module/chain.mjs` (LEDGER A9).

## Environment (checked)
PC wifi 10.0.0.71/24 · PS4 Fat 10.0.0.12 answers ping · ufw allows 9898 (10.0.0.0/24) and 9031 ·
containers `webkitty` (:9898) and `diag-receiver` (:9031) up · served goldhen_v2.4b18.14.bin sha256 8382b37a… == diag.bin.
Start/restart: `rebuild/webkitty-docker/run.sh`.
