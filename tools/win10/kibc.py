import sys, mmap, struct
f = open(sys.argv[1], 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
def u16(o): return struct.unpack_from('<H', m, o)[0]
def u32(o): return struct.unpack_from('<I', m, o)[0]
i = m.find(b'ntoskrnl.exe\x00')
seen = set()
while i != -1:
    # walk back page by page for an MZ header whose export directory name is ntoskrnl.exe
    for base in range(i & ~0xfff, max(0, (i & ~0xfff) - 0x2000000), -0x1000):
        if m[base:base+2] != b'MZ': continue
        pe = base + u32(base + 0x3c)
        if pe + 0x200 > len(m) or m[pe:pe+4] != b'PE\0\0': continue
        opt = pe + 24
        if u16(opt) != 0x20b: continue
        erva, esz = u32(opt + 112), u32(opt + 116)
        ed = base + erva
        if ed + 40 > len(m) or erva == 0: continue
        name = m[base + u32(ed + 12): base + u32(ed + 12) + 12]
        if name != b'ntoskrnl.exe' or base in seen: break
        seen.add(base)
        n, funcs, names, ords = u32(ed + 24), base + u32(ed + 28), base + u32(ed + 32), base + u32(ed + 36)
        for k in range(n):
            nm_off = base + u32(names + 4 * k)
            if m[nm_off:nm_off + 15] == b'KiBugCheckData\x00':
                rva = u32(funcs + 4 * u16(ords + 2 * k))
                v = struct.unpack_from('<5Q', m, base + rva)
                print('ntoskrnl at phys %#x, KiBugCheckData rva %#x:' % (base, rva), ' '.join(hex(x) for x in v))
        break
    i = m.find(b'ntoskrnl.exe\x00', i + 1)
