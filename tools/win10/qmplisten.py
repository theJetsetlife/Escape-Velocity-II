import socket, sys, time, json
p, out = sys.argv[1], sys.argv[2]
for _ in range(200):
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(p); break
    except OSError: time.sleep(0.1)
f = s.makefile('rwb', buffering=0)
f.readline(); f.write(b'{"execute":"qmp_capabilities"}\n')
with open(out, 'w') as o:
    for line in f:
        o.write(line.decode()); o.flush()
