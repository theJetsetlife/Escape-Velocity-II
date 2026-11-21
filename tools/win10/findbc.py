import sys, mmap, struct, collections
f = open(sys.argv[1], 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pat = struct.pack('<Q', 0xA5); hits = collections.Counter(); where = {}
i = m.find(pat)
while i != -1:
    if i % 8 == 0 and i + 40 <= len(m):
        p = struct.unpack_from('<5Q', m, i)
        if p[1] != 0 and p[1] < 0x200000:
            key = p[1:]; hits[key] += 1; where.setdefault(key, i)
    i = m.find(pat, i + 1)
for k, n in hits.most_common(15):
    print(n, hex(where[k]), ' '.join(hex(x) for x in k))
