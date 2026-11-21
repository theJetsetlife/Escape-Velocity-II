#!/usr/bin/env python3
"""Decode a k0probe capture (reports/diag-*.bin with HELLO version 0x200).

    python3 -I tools/k0probe_decode.py reports/diag-YYYYMMDD-HHMMSS.bin [--json out.json]

Prints OrbisOS's own RAM bookkeeping: phys_avail, dump_avail, physmap copy, vm_phys segments,
the loader SMAP (MODINFOMD_SMAP 0x9001 inside preload metadata), memory MSRs, live PTEs, and
where every step ended. The receiver already keeps the raw .bin; this file never writes it.
"""
import json, struct, sys

MAGIC = b"DBDIAG01"
T = dict(HELLO=1, STAGE=2, MSR=4, GATE=5, ERR=9, NOTE=11, RET=12, END=13, PTE=14, KMEM=16)
TN = {v: k for k, v in T.items()}
KM = {1: "phys_avail", 2: "dump_avail", 3: "physmap_copy", 4: "memvar_A(+0x21fa1f8)",
      5: "memvar_B(+0x21fb250)", 6: "memvar_C(+0x2216770)", 7: "pmapvars(+0x1b90480)",
      8: "vm_phys_hdr", 9: "vm_phys_segs", 10: "preload_metadata"}
MSRN = {0xc001001a: "TOP_MEM", 0xc001001d: "TOM2", 0xc0010010: "SYSCFG", 0xc0010058: "MMIO_CFG_BASE"}
SMAP_T = {1: "RAM", 2: "reserved", 3: "ACPI", 4: "NVS", 5: "unusable"}
MODINFO = {0x0001: "NAME", 0x0002: "TYPE", 0x0003: "ARGS", 0x0004: "LOADADDR", 0x0005: "SIZE",
           0x0006: "EMPTY", 0x0007: "DEPS"}
MODINFOMD = {0x0001: "AOUTEXEC", 0x0002: "ELFHDR", 0x0003: "SSYM", 0x0004: "ESYM", 0x0005: "DYNAMIC",
             0x0006: "ENVP", 0x0007: "HOWTO", 0x0008: "KERNEND", 0x0009: "SHDR", 0x1001: "SMAP",
             0x1002: "SMAP_XATTR", 0x1003: "DTBP", 0x1004: "EFI_MAP", 0x1005: "EFI_FB"}


def records(data):
    if not data.startswith(MAGIC):
        raise SystemExit("not a diag capture")
    o = 8
    while o + 24 <= len(data):
        tag, ln, a, b = struct.unpack_from("<IIQQ", data, o)
        o += 24
        body = data[o:o + ln]
        o += (ln + 7) & ~7
        yield tag, a, b, body
        if len(body) < ln:
            yield -1, 0, 0, b""
            return


def qwords(b):
    return list(struct.unpack("<%dQ" % (len(b) // 8), b[:len(b) // 8 * 8]))


def ranges(q):
    out = []
    for i in range(0, len(q) - 1, 2):
        if q[i] == 0 and q[i + 1] == 0:
            break
        out.append((q[i], q[i + 1]))
    return out


def mib(n):
    return n / 2**20


def decode_md(body, base):
    """walk MODINFO records; returns (lines, smap list)"""
    L, smap, o = [], [], 0
    while o + 8 <= len(body):
        t, s = struct.unpack_from("<II", body, o)
        if t == 0 and s == 0:
            L.append("     end of metadata (+%#x)" % o)
            break
        d = body[o + 8:o + 8 + s]
        if t & 0x8000:
            name = "MD_" + MODINFOMD.get(t & 0x7fff, "%#x" % (t & 0x7fff))
        else:
            name = MODINFO.get(t, "%#x" % t)
        if t in (1, 2, 3):
            val = d.rstrip(b"\0").decode(errors="replace")
        elif s == 8:
            val = "%#x" % struct.unpack("<Q", d)[0]
        elif s == 4:
            val = "%#x" % struct.unpack("<I", d)[0]
        else:
            val = "%d bytes" % s
        L.append("     %-18s %s" % (name, val))
        if t == 0x9001:
            for k in range(0, s - 19, 20):
                b, ln, ty = struct.unpack_from("<QQI", d, k)
                smap.append((b, ln, ty))
        o += 8 + ((s + 7) & ~7)
    return L, smap


def decode(data):
    L, J = [], {"steps": {}, "errors": []}
    p = L.append
    last = None
    kbase = None
    for tag, a, b, body in records(data):
        n = TN.get(tag, "?%d" % tag)
        if tag == T["HELLO"]:
            p("== HELLO version %#x%s" % (a, " (k0probe)" if a == 0x200 else " (NOT k0probe)"))
        elif tag == T["STAGE"]:
            last = (a, b)
            p("\n== step %d %s: %s" % (a - 30, body.decode(errors="replace"), "ENTER" if b == 0 else "LEAVE"))
            if b:
                J["steps"][a - 30] = "done"
        elif tag == T["GATE"]:
            p("   [%s] gate %d: %s" % ("PASS" if b else "FAIL", a, body.decode(errors="replace")))
        elif tag == T["NOTE"]:
            s = body.decode(errors="replace")
            p("   note %s: a=%#x b=%#x" % (s, a, b))
            if s == "kbase|dmap":
                kbase = a
                J["kbase"], J["dmap"] = a, b
            if s.startswith("kernel0 copy"):
                J["kblob"] = a
        elif tag == T["ERR"]:
            p("   !! ERROR %d detail=%#x: %s" % (a, b, body.decode(errors="replace")))
            J["errors"].append((a, b, body.decode(errors="replace")))
        elif tag == T["RET"]:
            p("   kexec(step %d) returned %d" % (a - 30, struct.unpack("<q", struct.pack("<Q", b))[0]))
        elif tag == T["MSR"]:
            p("   MSR %-14s %#018x" % (MSRN.get(a, "%#x" % a), b))
            J.setdefault("msr", {})[MSRN.get(a, hex(a))] = b
        elif tag == T["PTE"]:
            lvl = struct.unpack("<Q", body[:8])[0] if len(body) >= 8 else 0
            if lvl:
                sz = {1: 1 << 12, 2: 1 << 21, 3: 1 << 30}[lvl]
                pa = (b & 0x000ffffffffff000) & ~(sz - 1) | (a & (sz - 1))
                p("   PTE va %#x -> pa %#x (%s, %s%s)" % (a, pa, {1: "4K", 2: "2M", 3: "1G"}[lvl],
                                                      "RW " if b & 2 else "RO ", "NX" if b >> 63 else "X"))
                J.setdefault("pte", {})[hex(a)] = pa
            else:
                p("   PTE va %#x not present" % a)
        elif tag == T["KMEM"]:
            name = KM.get(b, "id%d" % b)
            q = qwords(body)
            if b in (1, 2, 3):
                rr = ranges(q)
                if b == 3:     # header seen in the dump: 1, 0, <x>, <y>, then pairs
                    p("   %s (+%#x) header: %s" % (name, a, " ".join("%#x" % x for x in q[:4])))
                    rr = ranges(q[4:])
                p("   %s (+%#x): %d ranges, %.1f MiB" % (name, a, len(rr), mib(sum(e - s for s, e in rr))))
                for s, e in rr:
                    p("      %#011x - %#011x  %9.2f MiB" % (s, e, mib(e - s)))
                J[name] = rr
            elif b == 9:
                segs = []
                for i in range(0, len(q) - 4, 5):
                    st, en, fp, dom, fq = q[i:i + 5]
                    if st == 0 and en == 0:
                        break
                    segs.append((st, en, fp, dom, fq))
                p("   vm_phys_segs (+%#x): %d segments" % (a, len(segs)))
                for st, en, fp, dom, fq in segs:
                    p("      %#011x - %#011x %9.2f MiB  first_page %#x dom %d freeq %#x" % (st, en, mib(en - st), fp, dom, fq))
                J["vm_phys_segs"] = [(s[0], s[1]) for s in segs]
            elif b == 10:
                p("   preload metadata @ %#x (%d bytes)%s" % (a, len(body), " = kbase+%#x" % (a - kbase) if kbase else ""))
                ml, smap = decode_md(body, a)
                L.extend(ml)
                if smap:
                    p("   loader SMAP (%d entries):" % len(smap))
                    for s, ln, ty in smap:
                        p("      %#011x - %#011x %9.2f MiB  %s" % (s, s + ln, mib(ln), SMAP_T.get(ty, "type %d" % ty)))
                J["smap"] = smap
            else:
                p("   %s (+%#x): %s" % (name, a, " ".join("%#x" % x for x in q)))
                J[name] = q
        elif tag == T["END"]:
            p("\n== END: %d records" % a)
            J["end"] = True
        elif tag == -1:
            p("!! STREAM TRUNCATED")
        else:
            p("   %s a=%#x b=%#x len=%d" % (n, a, b, len(body)))
    if "end" not in J:
        p("\n!! no END record: console stopped %s" %
          ("in step %d (%s)" % (last[0] - 30, "inside ring0" if last[1] == 0 else "after it") if last else "before step 0"))
    return "\n".join(L) + "\n", J


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    txt, J = decode(open(sys.argv[1], "rb").read())
    sys.stdout.write(txt)
    if "--json" in sys.argv:
        out = sys.argv[sys.argv.index("--json") + 1]
        json.dump(J, open(out, "w"), indent=1, default=lambda x: x)


if __name__ == "__main__":
    main()
