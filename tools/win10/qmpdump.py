#!/usr/bin/env python3
"""qmpdump.py <qmp socket> <ram out> - wait for QEMU's GUEST_PANICKED event and save guest RAM (first 2 GiB)
with pmemsave, so tools/win10/kibc.py can read the Windows bugcheck record. Prints every QMP line."""
import json, socket, sys, time
path, ram = sys.argv[1], sys.argv[2]
for _ in range(300):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(path); break
    except OSError:
        time.sleep(0.1)
f = s.makefile('rwb', buffering=0)
f.readline(); f.write(b'{"execute":"qmp_capabilities"}\n')
for line in f:
    ev = json.loads(line)
    if ev.get('event') == 'GUEST_PANICKED':
        f.write(json.dumps({"execute": "pmemsave", "arguments": {"val": 0, "size": 0x80000000, "filename": ram}}).encode() + b'\n')
    print(line.decode().strip()[:200], flush=True)
