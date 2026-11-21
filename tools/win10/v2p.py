import sys, mmap, struct
F = open(sys.argv[1], "rb"); m = mmap.mmap(F.fileno(), 0, access=mmap.ACCESS_READ)
cr3 = int(sys.argv[2],16); va = int(sys.argv[3],16); n = int(sys.argv[4]) if len(sys.argv) > 4 else 256
def q(pa): return struct.unpack_from('<Q', m, pa)[0]
def v2p(va):
    t = cr3 & ~0xfff
    for lvl, sh in enumerate((39,30,21,12)):
        e = q(t + ((va >> sh) & 0x1ff) * 8)
        if not e & 1: return None
        if lvl in (1,2) and e & 0x80:
            return (e & 0xffffffffff000 & ~((1<<sh)-1)) | (va & ((1<<sh)-1))
        t = e & 0xffffffffff000
    return t | (va & 0xfff)
pa = v2p(va); print('va %#x -> pa %s' % (va, hex(pa) if pa is not None else None))
if pa is not None:
    b = m[pa - 64: pa + n]
    for o in range(0, len(b), 16):
        ch = b[o:o+16]; print('%#x: %s  %s' % (pa - 64 + o, ch.hex(' '), ''.join(chr(c) if 32 <= c < 127 else '.' for c in ch)))
