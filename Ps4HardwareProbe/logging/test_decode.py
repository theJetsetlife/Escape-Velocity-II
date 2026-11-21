#!/usr/bin/env python3
"""test_decode.py DIR - decode every record stream the host test wrote and check the reports match the scenario."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_recv as pr

d = sys.argv[1]
fails = 0
want = {
    "fat11-aeolia-900": ("PS4 CUH-10xx/11xx", "Aeolia", "hardware-verified"),
    "fat12-belize-900-level2": ("PS4 CUH-12xx", "Belize", "hardware-verified"),
    "slim-baikal-1100-validated": ("PS4 Slim", "Baikal", "validated at runtime only"),
    "pro-baikal-unknown-fw": ("unknown", "none", "unknown (no kernel offsets used)"),
    "unknown-hardware": ("unknown", "unknown/mixed Sony", "hardware-verified"),
}
for name, (board, sb, fw) in want.items():
    r = pr.analyse(open(os.path.join(d, name + ".bin"), "rb").read())
    txt = pr.report(r)
    ok = board in txt and ("southbridge    : " + sb) in txt and fw in txt and r["end"] is not None and bool(r["diag"])
    print("  %s %s" % ("ok  " if ok else "FAIL", name))
    fails += not ok
cmp = pr.compare(open(os.path.join(d, "fat11-aeolia-900.bin"), "rb").read(),
                 open(os.path.join(d, "slim-baikal-1100-validated.bin"), "rb").read())
ok = "gpu_device" in cmp and "sb_family" in cmp
print("  %s compare fat11 vs slim reports GPU + southbridge differences" % ("ok  " if ok else "FAIL"))
fails += not ok
print("decoder RESULT:", "PASS" if not fails else "FAIL")
sys.exit(1 if fails else 0)
