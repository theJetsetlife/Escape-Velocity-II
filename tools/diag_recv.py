#!/usr/bin/env python3
"""PC side of the read-only diagnostic payload (src/diag).

  diag_recv.py listen [--port 9031] [--out DIR]   keep receiving runs (.bin + decoded .txt);
                                                  browsers hitting this port are redirected to WebKitty
  diag_recv.py decode FILE.bin                    re-decode a saved capture
  diag_recv.py patch  diag.bin|directboot.bin --ip A.B.C.D [--port N]   retarget a built blob
  diag_recv.py send   [diag.bin] [--host 10.0.0.12] [--port 9020]
                                                  send ONCE to WebKitty's Bin Loader (Tools -> Bin Loader)

Wire format: see src/diag/diag.h. Everything here is decoding only; it never sends
anything to the console.
"""
import argparse, datetime, os, socket, struct, sys

MAGIC = b"DBDIAG01"
CFG_MAGIC = b"DBDIAGCF"
T = dict(HELLO=1, STAGE=2, REGS=3, MSR=4, GATE=5, PCICFG=6, MMIO32=7, PHYS=8, ERR=9,
         SYSCTL=10, NOTE=11, RET=12, END=13, PTE=14, RUN=15)
TN = {v: k for k, v in T.items()}
ERR = {1: "FW_GATE (not retail 9.00) - stopped", 2: "DMAP", 3: "NOT_MAPPED", 4: "NOT_UC (skipped)",
       5: "NO_ECAM (PCI phases skipped)", 6: "NO_DEV", 7: "NO_BAR", 8: "NO_RSDP", 9: "ACPI_BAD",
       10: "SOCKET", 11: "KEXEC_RET", 12: "BUFFER_FULL"}
PHASE = {1: "ident", 2: "cpu", 3: "pgtbl", 4: "pci", 5: "glue", 6: "iommu", 7: "gpu", 8: "acpi", 9: "dmap",
         10: "dce probe A", 11: "dce probe B", 20: "arm: checks+patches", 21: "arm: alloc elf",
         22: "arm: alloc region", 23: "arm: alloc boot", 24: "arm: alloc fb", 25: "arm: finish",
         3: "install hook"}
MSRN = {0x1b: "APIC_BASE", 0xfe: "MTRRcap", 0x2ff: "MTRRdefType", 0x250: "MTRRfix64K_00000",
        0x258: "MTRRfix16K_80000", 0x259: "MTRRfix16K_A0000", 0x277: "PAT", 0xc0010010: "SYSCFG",
        0xc0010015: "HWCR", 0xc001001a: "TOP_MEM", 0xc001001d: "TOM2", 0xc0010058: "MMIO_CFG_BASE"}
MTYPE = {0: "UC", 1: "WC", 4: "WT", 5: "WP", 6: "WB", 7: "UC-"}
REGS_FMT = "<3Q4I5Q6I"            # struct diag_regs, 104 bytes
REGS_NAMES = ["kbase", "lstar", "dmap", "pml4pml4i", "dmpml4i", "dmpdpi", "cpu_maxpa",
              "cr0", "cr3", "cr4", "efer", "rflags", "cpuid1_eax", "cpuid1_ebx",
              "cpuid1_ecx", "cpuid1_edx", "cpuid7_ebx", "apic_id"]
GMC = [(0x809, "MC_VM_FB_LOCATION"), (0x80c, "MC_VM_AGP_BASE"), (0x80d, "MC_VM_SYSTEM_APERTURE_LOW_ADDR"),
       (0x80e, "MC_VM_SYSTEM_APERTURE_HIGH_ADDR"), (0x81a, "MC_VM_FB_OFFSET")]
GRPH = [(0x1a00, "GRPH_ENABLE"), (0x1a01, "GRPH_CONTROL"), (0x1a04, "GRPH_PRIMARY_SURFACE_ADDRESS"),
        (0x1a06, "GRPH_PITCH"), (0x1a07, "GRPH_PRIMARY_SURFACE_ADDRESS_HIGH"), (0x1a0d, "GRPH_X_END"),
        (0x1a0e, "GRPH_Y_END"), (0x1b9c, "CRTC_CONTROL")]
AEOLIA_FUNCS = ["acpi", "gbe", "ahci", "sdhci", "pcie(glue)", "dmac", "mem", "xhci"]


def records(data):
    if not data.startswith(MAGIC):
        raise SystemExit("not a diag capture (bad magic)")
    o = 8
    while o + 24 <= len(data):
        tag, ln, a, b = struct.unpack_from("<IIQQ", data, o)
        o += 24
        body = data[o:o + ln]
        o += (ln + 7) & ~7
        yield tag, a, b, body
        if len(body) < ln:
            yield -1, 0, 0, b"truncated stream"
            return


def u32s(b):
    return struct.unpack("<%dI" % (len(b) // 4), b[: len(b) // 4 * 4])


def decode(data, outdir=None):
    L = []
    p = L.append
    decode.runs = []
    acpi = []
    last_stage = None
    for tag, a, b, body in records(data):
        n = TN.get(tag, "?%d" % tag)
        if tag == T["HELLO"]:
            p("== HELLO version %d stage %d (payload executing in userland)" % (a, b))
        elif tag == T["STAGE"]:
            last_stage = (a, b)
            name = body.rstrip(b"\0").decode(errors="replace") if body else PHASE.get(a, "?")   # payload's own name wins
            p("\n== phase %d %s: %s" % (a, name, "ENTER" if b == 0 else "LEAVE"))
        elif tag == T["RET"]:
            p("   kexec(phase %d) returned %d" % (a, struct.unpack("<q", struct.pack("<Q", b))[0]))
        elif tag == T["GATE"]:
            p("   [%s] gate %d: %s" % ("PASS" if b else "FAIL", a, body.decode(errors="replace")))
        elif tag == T["ERR"]:
            if len(body) > 4 and all(32 <= ch < 127 for ch in body.rstrip(b"\0")):
                p("   !! ERR step %d detail=0x%x: %s" % (a, b, body.rstrip(b"\0").decode()))   # directboot text error
            else:
                extra = (" len=%d" % struct.unpack("<I", body[:4])[0]) if len(body) >= 4 else ""
                p("   !! ERR %s detail=0x%x%s" % (ERR.get(a, a), b, extra))
        elif tag == T["NOTE"]:
            t = body.decode(errors="replace")
            if t == "pml4[pml4pml4i]":
                p("   PML4[%#x] (recursive slot) = %#x -> phys %#x" % (a, b, b & 0xffffffffff000))
            elif t == "memtype mtrr|pat<<8":
                p("   memtype @%#x: MTRR=%s PAT=%s" % (a, MTYPE.get(b & 0xff, b & 0xff), MTYPE.get(b >> 8, b >> 8)))
            else:
                p("   note %s: a=%#x b=%#x" % (t, a, b))
        elif tag == T["REGS"]:
            v = dict(zip(REGS_NAMES, struct.unpack(REGS_FMT, body[:104])))
            p("   regs:" + "".join("\n     %-11s %#x" % (k, v[k]) for k in REGS_NAMES if v[k] or k in ("kbase", "dmap")))
        elif tag == T["MSR"]:
            nm = MSRN.get(a) or ("MTRRphys%s%d" % ("Base" if a % 2 == 0 else "Mask", (a - 0x200) // 2) if 0x200 <= a < 0x220
                                 else "MTRRfix4K_%X" % a if 0x268 <= a <= 0x26f else "")
            p("   msr %#010x %-18s = %#018x" % (a, nm, b))
        elif tag == T["PTE"]:
            lvl = struct.unpack("<Q", body[:8])[0]
            sz = {1: "4K", 2: "2M", 3: "1G"}.get(lvl, "NOT PRESENT")
            p("   pte  %#014x -> %s leaf=%#018x" % (a, sz, b))
        elif tag == T["PCICFG"]:
            d = u32s(body)
            dev, fn = (a >> 8) & 0xff, a & 0xff
            bars = " ".join("%08x" % d[4 + i] for i in range(6))
            p("   pci 00:%02x.%x %04x:%04x class %06x hdr %02x  BARs %s  [%s]" %
              (dev, fn, d[0] & 0xffff, d[0] >> 16, d[2] >> 8, (d[3] >> 16) & 0xff, bars, "ECAM" if b == 1 else "CF8"))
        elif tag == T["MMIO32"]:
            d = u32s(body)
            rg, idx = b & 0xff, b >> 8
            if rg == 1:
                p("   glue BAR windows (BAR4+0x1c8000 @ %#x)  [func*0x30 + bar*8: MASK, +4: ADDR]" % a)
                for f in range(8):
                    row = []
                    for bar in range(6):
                        o = (f * 0x30 + bar * 8) // 4
                        if o + 1 < len(d) and (d[o] or d[o + 1]):
                            row.append("bar%d mask=%08x addr=%08x" % (bar, d[o], d[o + 1]))
                    p("     func %d %-10s %s" % (f, AEOLIA_FUNCS[f], "; ".join(row) or "(all zero)"))
            elif rg == 2:
                p("   glue MSI block (BAR4+0x1c8400 @ %#x)" % a)
                p("     MSI_CONTROL  +0x400 = %08x" % d[0])
                for off in (0x408, 0x43c, 0x44c):
                    p("     H2b candidate +%#x = %08x" % (off + 0x1c8000, d[(off - 0x400) // 4]))
                for f in range(8):
                    p("     func %d MASK(+0x%x)=%08x ADDR(+0x%x)=%08x DATA_HI(+0x%x)=%08x" %
                      (f, 0x44c + f * 4, d[(0x4c + f * 4) // 4], 0x4ac + f * 4, d[(0xac + f * 4) // 4],
                       0x48c + f * 4, d[(0x8c + f * 4) // 4]))
                p("     raw:" + "".join(("\n       +%03x:" % (i * 4) if i % 8 == 0 else "") + " %08x" % x for i, x in enumerate(d)))
            elif rg == 3:
                p("   chip id (BAR2+0x1100 @ %#x): %s" % (a, " ".join("%08x" % x for x in d)))
            elif rg in (4, 5):
                p("   iommu %s @ %#x:%s" % ("lo" if rg == 4 else "hi(+0x2000)", a,
                  "".join(("\n     +%03x:" % (i * 4) if i % 4 == 0 else "") + " %08x" % x for i, x in enumerate(d))))
            elif rg == 6:
                p("   gpu MC (BAR5 @ %#x):" % a)
                for (reg, nm), x in zip(GMC, d):
                    p("     %-34s = %08x" % (nm, x))
                fbloc = dict(zip([g[0] for g in GMC], d))
                loc, off = fbloc.get(0x809, 0), fbloc.get(0x81a, 0)
                p("     -> MC FB range %#x..%#x, FB_OFFSET<<22 = %#x (inference: CPU phys of FB start)" %
                  ((loc & 0xffff) << 24, ((loc >> 16) << 24) | 0xffffff, off << 22))
                decode.mc = ((loc & 0xffff) << 24, off << 22)
            elif rg == 10:
                RN = {0x1a00: "GRPH_ENABLE", 0x1a01: "GRPH_CONTROL", 0x1a03: "SWAP_CNTL", 0x1a04: "PRIMARY_SURF",
                      0x1a05: "SECONDARY_SURF", 0x1a06: "PITCH", 0x1a07: "PRIMARY_HI", 0x1a08: "SECONDARY_HI",
                      0x1a0d: "X_END", 0x1a0e: "Y_END", 0x1a11: "GRPH_UPDATE", 0x1a12: "FLIP_CONTROL",
                      0x1a13: "SURF_INUSE", 0x1a18: "SURF_HI_INUSE", 0x1a1c: "OVL_ENABLE", 0x1a1d: "OVL_CONTROL1",
                      0x1a20: "OVL_SURF", 0x1a21: "OVL_PITCH", 0x1a22: "OVL_SURF_HI", 0x1b9c: "CRTC_CONTROL",
                      0x1bbd: "MASTER_UPDATE_LOCK", 0x1bbe: "MASTER_UPDATE_MODE"}
                pairs = [(d[i], d[i + 1]) for i in range(0, len(d) - 1, 2)]
                p("   DCE crtc%d probe %s: %s" % (idx & 0xff, "AB"[(b >> 16) & 1],
                  "  ".join("%s=%08x" % (RN.get(r, hex(r)), v) for r, v in pairs)))
            elif rg in (8, 9):
                names = ({0x00: "GRPH_ENABLE", 0x01: "GRPH_CONTROL", 0x03: "GRPH_SWAP_CNTL", 0x04: "PRIMARY_SURF",
                          0x05: "SECONDARY_SURF", 0x06: "GRPH_PITCH", 0x07: "PRIMARY_SURF_HI", 0x08: "SECONDARY_SURF_HI",
                          0x0d: "GRPH_X_END", 0x0e: "GRPH_Y_END", 0x11: "GRPH_UPDATE", 0x12: "GRPH_FLIP_CONTROL",
                          0x13: "SURF_ADDR_INUSE", 0x18: "SURF_ADDR_HI_INUSE", 0x1c: "OVL_ENABLE", 0x1d: "OVL_CONTROL1",
                          0x20: "OVL_SURF", 0x21: "OVL_PITCH", 0x22: "OVL_SURF_HI"} if rg == 8 else
                         {0x00: "CRTC_CONTROL", 0x08: "CRTC_STATUS_POSITION", 0x0a: "CRTC_STATUS_FRAME_COUNT",
                          0x21: "MASTER_UPDATE_LOCK", 0x22: "MASTER_UPDATE_MODE"})
                if rg == 8:
                    p("   DCE crtc%d (0x1a00..): %s" % (idx, "  ".join("%s=%08x" % (nm, d[o]) for o, nm in sorted(names.items()) if o < len(d))))
                else:
                    p("   DCE crtc%d (0x1b9c..): %s" % (idx, "  ".join("%s=%08x" % (nm, d[o]) for o, nm in sorted(names.items()) if o < len(d))))
            elif rg == 7:
                v = {nm: x for (reg, nm), x in zip(GRPH, d)}
                surf = (v["GRPH_PRIMARY_SURFACE_ADDRESS_HIGH"] << 32) | (v["GRPH_PRIMARY_SURFACE_ADDRESS"] & ~0xff)
                line = "   crtc%d: enable=%x ctrl=%08x crtc_ctl=%08x surface(MC)=%#x pitch=%d x_end=%d y_end=%d" % (
                    idx, v["GRPH_ENABLE"], v["GRPH_CONTROL"], v["CRTC_CONTROL"], surf, v["GRPH_PITCH"],
                    v["GRPH_X_END"], v["GRPH_Y_END"])
                mc = getattr(decode, "mc", None)
                if mc and v["GRPH_ENABLE"] & 1 and surf >= mc[0]:
                    line += "  -> fb_phys candidate %#x (surface - fb_base + fb_offset; VERIFY)" % (surf - mc[0] + mc[1])
                p(line)
        elif tag == T["PHYS"]:
            kind = b & 0xffff
            if kind == 1:
                q = struct.unpack("<512Q", body[:4096])
                p("   PML4 @ %#x: present slots %s" % (a, ", ".join("%#x->%#x" % (i, e & 0xffffffffff000) for i, e in enumerate(q) if e & 1)))
            elif kind == 2:
                q = struct.unpack("<512Q", body[:4096])
                pres = [(i, e) for i, e in enumerate(q) if e & 1]
                p("   PDPT (pml4 slot %#x) @ %#x: %d present; first: %s" % (b >> 16, a, len(pres),
                  ", ".join("%#x:%s%#x" % (i, "1G " if e & 0x80 else "", e & 0xffffffffff000) for i, e in pres[:12])))
            elif kind == 3:
                p("   RSDP @ %#x: oem=%r rev=%d rsdt=%#x%s" % (a, body[9:15], body[15], struct.unpack_from("<I", body, 16)[0],
                  (" xsdt=%#x" % struct.unpack_from("<Q", body, 24)[0]) if len(body) >= 32 else ""))
                acpi.append(("RSDP", a, body))
            elif kind == 6:
                p("   scanout crtc%d %s @ %#x: %s" % ((b >> 16) & 0xff, "centre" if (b >> 24) & 1 else "start",
                  a, " ".join("%08x" % x for x in u32s(body)[:16])))
            elif kind == 5:
                p("   kernel text via DMAP alias @pa %#x: match=%d bytes=%s (expect 0f01f865488924 25a8020000)" %
                  (a, b >> 16, body.hex()))
            elif kind == 4:
                sig = body[:4].decode(errors="replace")
                p("   ACPI %s @ %#x len %d oem=%r" % (sig, a, len(body), body[10:16]))
                acpi.append((sig, a, body))
        elif tag == T["SYSCTL"]:
            nm, _, val = body.partition(b"\0")
            st = struct.unpack("<q", struct.pack("<Q", a))[0]
            nm = nm.decode(errors="replace")
            if st < 0:
                p("   sysctl %-30s errno %d" % (nm, -st))
            elif nm == "machdep.smap" and val:
                es = 24 if len(val) % 24 == 0 else 20
                p("   sysctl machdep.smap (%d B entries):" % es)
                for i in range(0, len(val) - es + 1, es):
                    base, ln, ty = struct.unpack_from("<QQI", val, i)
                    p("     %#014x - %#014x  type %d" % (base, base + ln - 1, ty))
            elif len(val) in (4, 8) and not all(32 <= c < 127 or c == 0 for c in val):
                p("   sysctl %-30s = %d" % (nm, int.from_bytes(val, "little")))
            elif val and all(32 <= c < 127 or c in (9, 10, 0) for c in val):
                p("   sysctl %-30s = %s" % (nm, val.rstrip(b"\0").decode()))
            else:
                p("   sysctl %-30s = %s" % (nm, (" ".join("%#x" % x for x in struct.unpack("<%dQ" % (len(val) // 8), val[: len(val) // 8 * 8])) if len(val) >= 8 else val.hex())))
        elif tag == T["RUN"]:
            key = struct.unpack("<Q", body[:8])[0]
            if key == 0:
                desc = "not mapped"
            else:
                lvl = {1: "4K", 2: "2M", 3: "1G"}.get(key >> 8, "?")
                cache = ("PCD " if key & 16 else "") + ("PWT " if key & 8 else "") + ("PAT " if key & 32 else "")
                desc = "%s %s%s%s%s" % (lvl, "RW " if key & 2 else "RO ", "NX " if key & 64 else "X ",
                                         cache or "WB ", "USER" if key & 4 else "")
            decode.runs.append((a, b, key))
            p("   dmap %#011x-%#011x %8.1f MiB  %s" % (a, b - 1, (b - a) / 2**20, desc))
        elif tag == T["END"]:
            ok = b >> 8
            p("\n== END: %d records, buffer-full=%d, phases ok: %s" % (a, b & 1, ", ".join(PHASE[i] for i in PHASE if ok >> i & 1) or "none"))
        elif tag == -1:
            p("!! STREAM TRUNCATED")
        else:
            p("   %s a=%#x b=%#x len=%d" % (n, a, b, len(body)))
    if decode.runs:
        mapped = sum(b - a for a, b, k in decode.runs if k)
        low = sum(min(b, 1 << 32) - a for a, b, k in decode.runs if k and a < (1 << 32))
        p("   dmap total mapped: %.1f MiB (below 4 GiB %.1f MiB, above %.1f MiB)" %
          (mapped / 2**20, low / 2**20, (mapped - low) / 2**20))
    if not any(l.startswith("\n== END") for l in L):
        p("\n!! no END record: the console stopped during %s" %
          ("phase %d %s (%s)" % (last_stage[0], PHASE.get(last_stage[0]), "inside ring0" if last_stage[1] == 0 else "after it")
           if last_stage else "userland setup"))
    if outdir and acpi:
        os.makedirs(outdir, exist_ok=True)
        for sig, a, body in acpi:
            open(os.path.join(outdir, "%s_%x.dat" % (sig, a)), "wb").write(body)
        p("   (ACPI tables written to %s)" % outdir)
    return "\n".join(L) + "\n"


def handle(c, peer, args):
    """One connection: a diag capture (save + decode) or a browser (redirect to WebKitty)."""
    c.settimeout(120)
    try:
        head = c.recv(8, socket.MSG_PEEK)
    except socket.timeout:
        head = b""
    if head[:4] in (b"GET ", b"HEAD", b"POST"):
        req = b""
        try:
            while b"\r\n\r\n" not in req and len(req) < 8192:
                d = c.recv(4096)
                if not d:
                    break
                req += d
        except socket.timeout:
            pass
        host = c.getsockname()[0]           # fallback; inside docker this is the container IP
        for line in req.split(b"\r\n")[1:]:
            if line.lower().startswith(b"host:"):
                host = line.split(b":", 1)[1].strip().decode(errors="replace").rsplit(":", 1)[0]
        url = "http://%s:%d/" % (host, args.web_port)
        body = ('<html><body>This port is the diagnostic receiver. '
                'Redirecting to WebKitty: <a href="%s">%s</a></body></html>' % (url, url)).encode()
        c.sendall(b"HTTP/1.1 302 Found\r\nLocation: " + url.encode() +
                  b"\r\nContent-Type: text/html\r\nContent-Length: " + str(len(body)).encode() +
                  b"\r\nConnection: close\r\n\r\n" + body)
        print("browser from %s on the receiver port -> redirected to %s" % (peer[0], url), flush=True)
        return
    if head == b"DBUPLGET":                # directboot stage fetching the UEFI payload
        c.recv(8)
        try:
            data = open(args.upl, "rb").read()
        except OSError as e:
            print("!! UPL request but %s unreadable: %s" % (args.upl, e), flush=True)
            data = b""
        c.sendall(struct.pack("<Q", len(data)) + data)
        print("served %s (%d bytes) to %s" % (args.upl, len(data), peer[0]), flush=True)
        return
    if head != MAGIC:
        print("ignored connection from %s (not a diag capture: %r)" % (peer[0], head), flush=True)
        return
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base = os.path.join(args.out, "diag-%s" % stamp)
    print("diag payload connected from %s -> %s.bin" % (peer[0], base), flush=True)
    buf = bytearray()
    with open(base + ".bin", "wb") as f:
        try:
            while True:
                chunk = c.recv(65536)
                if not chunk:
                    break
                buf += chunk
                f.write(chunk)
                f.flush()            # survive a console crash mid-stream
        except socket.timeout:
            print("!! no data for 120 s - console probably stopped", flush=True)
        except OSError as e:
            print("!! connection error: %s" % e, flush=True)
    try:
        txt = decode(bytes(buf), base + "-acpi")
    except BaseException as e:            # never lose the raw capture over a decode bug
        txt = "!! decode failed: %r (raw capture kept in %s.bin)\n" % (e, base)
    open(base + ".txt", "w").write(txt)
    print(txt, flush=True)
    print("saved %s.bin / %s.txt" % (base, base), flush=True)


def cmd_listen(args):
    os.makedirs(args.out, exist_ok=True)
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", args.port))
    s.listen(4)
    print("receiver on port %d (browsers are redirected to WebKitty on :%d), Ctrl-C to stop"
          % (args.port, args.web_port), flush=True)
    import threading

    def serve(c, peer):
        try:
            handle(c, peer, args)
        except Exception as e:                 # one bad connection must not stop the receiver
            print("!! %s: %r" % (peer[0], e), flush=True)
        finally:
            c.close()

    # One thread per connection: the directboot stage keeps its log connection open while it
    # fetches the UPL on a second one (a sequential receiver deadlocks, HW attempt 19:11 UTC).
    while True:
        c, peer = s.accept()
        threading.Thread(target=serve, args=(c, peer), daemon=True).start()


def cmd_decode(args):
    data = open(args.file, "rb").read()
    print(decode(data, os.path.splitext(args.file)[0] + "-acpi"))


def cmd_patch(args):
    b = bytearray(open(args.bin, "rb").read())
    if b[0x10:0x18] not in (CFG_MAGIC, b"DBBOOTCF"):          # diag.bin or the directboot stage
        raise SystemExit("no DBDIAGCF/DBBOOTCF config block at 0x10 - not a diag.bin / directboot.bin?")
    o = 0x10
    b[o + 8:o + 12] = socket.inet_aton(args.ip)
    b[o + 12:o + 14] = struct.pack(">H", args.port)
    open(args.bin, "wb").write(b)
    print("patched %s -> %s:%d" % (args.bin, args.ip, args.port))


def cmd_send(args):
    """Send a .bin ONCE to WebKitty's Bin Loader (bundle.js runBinLoader, port 9020).
    No probing: the loader runs whatever it receives as soon as the peer closes,
    so an empty or partial connection makes it jump into an empty buffer."""
    data = open(args.bin, "rb").read()
    if not data:
        raise SystemExit("refusing to send an empty file")
    if len(data) > 0x300000:
        raise SystemExit("payload larger than the loader buffer (0x300000)")
    s = socket.create_connection((args.host, args.port), timeout=10)
    s.sendall(data)
    s.shutdown(socket.SHUT_WR)
    s.close()
    print("sent %s (%d bytes) to %s:%d - watch the receiver: docker logs -f diag-receiver"
          % (args.bin, len(data), args.host, args.port))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    a = sp.add_parser("listen"); a.add_argument("--port", type=int, default=9031)
    a.add_argument("--web-port", type=int, default=9898)
    a.add_argument("--upl", default=os.path.join(here, "..", "out", "upl-ps4.elf"),
                   help="UEFI payload served to the directboot stage (DBUPLGET)")
    a.add_argument("--out", default=os.path.join(here, "..", "reports")); a.set_defaults(f=cmd_listen)
    a = sp.add_parser("decode"); a.add_argument("file"); a.set_defaults(f=cmd_decode)
    a = sp.add_parser("patch"); a.add_argument("bin"); a.add_argument("--ip", required=True)
    a.add_argument("--port", type=int, default=9031); a.set_defaults(f=cmd_patch)
    a = sp.add_parser("send"); a.add_argument("bin", nargs="?", default=os.path.join(here, "..", "src", "diag", "diag.bin"))
    a.add_argument("--host", default="10.0.0.12"); a.add_argument("--port", type=int, default=9020); a.set_defaults(f=cmd_send)
    args = ap.parse_args()
    args.f(args)


if __name__ == "__main__":
    main()
