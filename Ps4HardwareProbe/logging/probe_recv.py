#!/usr/bin/env python3
"""probe_recv.py - PC side of Ps4HardwareProbe (decoding only; it never talks to the console except `send`).

  probe_recv.py listen  [--port 9033] [--out DIR]       keep receiving runs (.bin + decoded .txt + .json)
  probe_recv.py decode  RUN.bin [--json]                report: identity, profile, every diagnostic's status
  probe_recv.py compare A.bin B.bin                      hardware differences between two runs
  probe_recv.py patch   Ps4HardwareProbe.bin [--ip A.B.C.D] [--port N] [--env goldhen|webkitty]
                        [--level 0|1|2] [--strict 0|1] [--fw 900] [-o OUT]
  probe_recv.py send    Ps4HardwareProbe.bin --host CONSOLE [--port 9090]
                        send ONCE to the loader (GoldHEN BinLoader 9090, WebKitty 9020). Never probe the port.
Wire format: core/probe.h.
"""
import argparse, json, os, socket, struct, sys, time

MAGIC = b"PS4PRB01"
TAGS = {1: "HELLO", 2: "PHASE", 3: "DIAG", 4: "MSR", 5: "CPUID", 6: "CREG", 7: "PCICFG", 8: "MMIO32", 9: "PHYS",
        10: "SYSCTL", 11: "NOTE", 12: "RET", 13: "END", 15: "RUN", 16: "IDENT", 17: "FW", 18: "TSC", 19: "ACPI", 20: "CLOCK"}
STATUS = {0: "OK", 1: "UNSUPPORTED", 2: "SKIPPED (safety)", 3: "FAILED"}
DIAGS = {1: "environment", 2: "SMEP/SMAP gate", 6: "loader kexec", 3: "firmware (userland)", 4: "firmware (kernel profile)", 5: "direct map proof",
         10: "cpu identification", 11: "control registers", 12: "MSRs", 13: "cpu topology", 14: "TSC",
         20: "memory map (sysctl)", 21: "MTRRs", 22: "direct-map coverage", 30: "ACPI tables",
         40: "PCI ECAM", 41: "PCI scan", 42: "platform identification", 43: "southbridge identity", 44: "southbridge registers",
         50: "local APIC", 51: "MSI", 52: "IOAPIC", 60: "GPU identity", 61: "framebuffer metadata",
         70: "USB controllers", 80: "SATA", 81: "SDHCI", 90: "network", 100: "HPET"}
PHASES = ["", "fw", "cpu", "tsc", "pci", "sb", "irq", "gpu", "acpi", "dev", "mem"]
ENVS = {"unknown": 0, "webkitty": 1, "goldhen": 2}
SB = {0: "none", 1: "Aeolia", 2: "Belize", 3: "Baikal", 4: "unknown/mixed Sony"}
BOARD = {0: "unknown", 1: "PS4 CUH-10xx/11xx (GPU 0x9920)", 2: "PS4 CUH-12xx (GPU 0x9922)", 3: "PS4 Slim CUH-2xxx (GPU 0x9923)",
         4: "PS4 Pro CUH-7xxx (GPU 0x9924)"}
VSTATE = {0: "Unsupported / not yet tested", 1: "Experimental", 2: "Partially verified", 3: "Verified on physical hardware"}
FWSTATE = {0: "unknown (no kernel offsets used)", 1: "unsupported (no/rejected profile)",
           2: "identity confirmed (OpenGoldHEN), no DMAP profile", 3: "community profile, validated at runtime only",
           4: "hardware-verified profile"}
IDENT = struct.Struct("<HHBBBBBBBBBBBBHHI3IBBBB")
IDENT_FIELDS = ("gpu_device sb_glue_device gpu_found sb_family sb_funcs board board_state sb_state fw_state env ring0 level "
                "strict chipid_ok fw_kernel fw_user cpuid1_eax chip0 chip1 chip2 glue_bus glue_dev glue_fn profile_state").split()
CFG_OFF = 0x10


def records(data):
    if not data.startswith(MAGIC):
        raise ValueError("not a Ps4HardwareProbe stream (magic)")
    o = 8
    while o + 24 <= len(data):
        tag, ln, a, b = struct.unpack_from("<IIQQ", data, o)
        o += 24
        yield tag, a, b, data[o:o + ln]
        o += (ln + 7) & ~7


def cstr(b):
    return b.split(b"\0", 1)[0].decode("utf-8", "replace")


def analyse(data):
    r = {"diag": {}, "pci": [], "msr": {}, "acpi": [], "fw": [], "sysctl": {}, "notes": [], "ident": None, "tsc": {},
         "clock": {}, "phases": [], "end": None, "mmio": []}
    for tag, a, b, d in records(data):
        t = TAGS.get(tag, str(tag))
        if t == "DIAG": r["diag"][a] = (b, cstr(d))
        elif t == "PCICFG":
            vid, did = struct.unpack_from("<HH", d, 0)
            cr = struct.unpack_from("<I", d, 8)[0]
            r["pci"].append({"bdf": "%02x:%02x.%x" % (a >> 16, (a >> 8) & 0xff, a & 7), "vendor": vid, "device": did,
                             "class": cr >> 8, "msi": has_cap(d, 0x05), "msix": has_cap(d, 0x11)})
        elif t == "MSR": r["msr"][a] = b
        elif t == "ACPI": r["acpi"].append({"sig": d[:4].decode("ascii", "replace"), "phys": a, "len": struct.unpack_from("<I", d, 4)[0], "checksum_ok": bool(b)})
        elif t == "FW": r["fw"].append((a, b))
        elif t == "SYSCTL":
            name = cstr(d); val = d[len(name) + 1:len(name) + 1 + b]
            r["sysctl"][name] = val.split(b"\0")[0].decode("ascii", "replace") if val and all(32 <= c < 127 or c == 0 for c in val[:64]) else val.hex()[:64]
        elif t == "NOTE": r["notes"].append((a, b, cstr(d)))
        elif t == "IDENT" and len(d) >= IDENT.size: r["ident"] = dict(zip(IDENT_FIELDS, IDENT.unpack_from(d)))
        elif t == "TSC": r["tsc"][a] = b
        elif t == "CLOCK": r["clock"][a] = b
        elif t == "PHASE": r["phases"].append((a, b))
        elif t == "END": r["end"] = (a, b)
        elif t == "MMIO32": r["mmio"].append((a, b, len(d)))
    if 0 in r["tsc"] and 1 in r["tsc"] and 0 in r["clock"] and 1 in r["clock"] and r["clock"][1] > r["clock"][0]:
        r["tsc_hz"] = (r["tsc"][1] - r["tsc"][0]) * 1e9 / (r["clock"][1] - r["clock"][0])
    return r


def has_cap(cfg, cid):
    if len(cfg) < 0x40 or not (struct.unpack_from("<H", cfg, 6)[0] & 0x10):
        return False
    p, n = cfg[0x34] & 0xfc, 0
    while p and p + 2 <= len(cfg) and n < 48:
        if cfg[p] == cid:
            return True
        p, n = cfg[p + 1] & 0xfc, n + 1
    return False


def report(r):
    out = []
    i = r["ident"] or {}
    out.append("== Ps4HardwareProbe report ==")
    out.append("selected profile: %s + %s + firmware %s -> %s" % (BOARD.get(i.get("board"), "?"), SB.get(i.get("sb_family"), "?"),
               fmt_fw(i.get("fw_kernel") or i.get("fw_user")), VSTATE.get(i.get("profile_state"), "?")))
    out.append("console family : %s [%s]" % (BOARD.get(i.get("board"), "?"), VSTATE.get(i.get("board_state"), "?")))
    out.append("GPU            : %s" % ("1002:%04x" % i["gpu_device"] if i.get("gpu_found") else "not identified"))
    chip = ("chip id %08x:%08x rev %08x" % (i["chip0"], i["chip1"], i["chip2"])) if i.get("chipid_ok") else "chip id not read"
    out.append("southbridge    : %s (glue %04x, %d functions, %s) [%s]" % (SB.get(i.get("sb_family"), "?"), i.get("sb_glue_device", 0),
               i.get("sb_funcs", 0), chip, VSTATE.get(i.get("sb_state"), "?")))
    out.append("firmware       : libc.sprx %s, kernel profile %s -> %s" % (fmt_fw(i.get("fw_user")), fmt_fw(i.get("fw_kernel")),
               FWSTATE.get(i.get("fw_state"), "?")))
    out.append("environment    : %s, ring0 %s, read level %s, strict %s" % ({1: "WebKitty", 2: "GoldHEN"}.get(i.get("env"), "?"),
               "yes" if i.get("ring0") else "no", i.get("level"), i.get("strict")))
    if "cpuid1_eax" in i:
        e = i["cpuid1_eax"]; fam = ((e >> 8) & 0xf) + (((e >> 20) & 0xff) if ((e >> 8) & 0xf) == 0xf else 0)
        out.append("cpu            : family %xh model %xh stepping %x" % (fam, ((e >> 4) & 0xf) | (((e >> 16) & 0xf) << 4), e & 0xf))
    if "tsc_hz" in r:
        out.append("TSC            : %.1f MHz (vs CLOCK_MONOTONIC)" % (r["tsc_hz"] / 1e6))
    out.append("")
    out.append("diagnostics:")
    for k in sorted(r["diag"]):
        st, why = r["diag"][k]
        out.append("  %-26s %-17s %s" % (DIAGS.get(k, "diag %d" % k), STATUS.get(st, st), why))
    if r["pci"]:
        out.append(""); out.append("PCI functions:")
        for p in r["pci"]:
            out.append("  %s %04x:%04x class %06x%s%s" % (p["bdf"], p["vendor"], p["device"], p["class"], " MSI" if p["msi"] else "", " MSI-X" if p["msix"] else ""))
    if r["acpi"]:
        out.append(""); out.append("ACPI tables:")
        for t in r["acpi"]:
            out.append("  %s @%#x len %d checksum %s" % (t["sig"], t["phys"], t["len"], "ok" if t["checksum_ok"] else "BAD"))
    if r["sysctl"]:
        out.append(""); out.append("sysctl:")
        for k, v in r["sysctl"].items():
            out.append("  %-22s %s" % (k, v))
    last = [p for p in r["phases"] if p[1] == 0]
    if r["end"] is None and last:
        out.append(""); out.append("!! stream ended without END: last phase entered = %s" % PHASES[last[-1][0]])
    return "\n".join(out)


def fmt_fw(v):
    return "%d.%02d" % (v // 100, v % 100) if v else "?"


def compare(a, b):
    ra, rb = analyse(a), analyse(b)
    out = []
    ia, ib = ra["ident"] or {}, rb["ident"] or {}
    for k in ("board", "gpu_device", "sb_family", "sb_glue_device", "chip0", "chip1", "chip2", "fw_user", "fw_kernel", "cpuid1_eax"):
        if ia.get(k) != ib.get(k):
            out.append("ident %-15s %s -> %s" % (k, ia.get(k), ib.get(k)))
    pa = {p["bdf"]: p for p in ra["pci"]}; pb = {p["bdf"]: p for p in rb["pci"]}
    for bdf in sorted(set(pa) | set(pb)):
        x, y = pa.get(bdf), pb.get(bdf)
        if not x or not y or (x["vendor"], x["device"], x["class"]) != (y["vendor"], y["device"], y["class"]):
            out.append("pci %s %s -> %s" % (bdf, x and "%04x:%04x" % (x["vendor"], x["device"]), y and "%04x:%04x" % (y["vendor"], y["device"])))
    for m in sorted(set(ra["msr"]) | set(rb["msr"])):
        if ra["msr"].get(m) != rb["msr"].get(m):
            out.append("msr %#x %s -> %s" % (m, hex(ra["msr"][m]) if m in ra["msr"] else None, hex(rb["msr"][m]) if m in rb["msr"] else None))
    for d in sorted(set(ra["diag"]) | set(rb["diag"])):
        sa, sb = ra["diag"].get(d, (None,))[0], rb["diag"].get(d, (None,))[0]
        if sa != sb:
            out.append("diag %-26s %s -> %s" % (DIAGS.get(d, d), STATUS.get(sa), STATUS.get(sb)))
    return "\n".join(out) or "no differences"


def patch(path, ip, port, env, level, strict, fw, outp):
    b = bytearray(open(path, "rb").read())
    if b[CFG_OFF:CFG_OFF + 8] != b"PRBCFG01":
        sys.exit("no PRBCFG01 config block at 0x10")
    if ip: b[CFG_OFF + 8:CFG_OFF + 12] = socket.inet_aton(ip)
    if port is not None: b[CFG_OFF + 12:CFG_OFF + 14] = struct.pack(">H", port)
    if env: b[CFG_OFF + 14] = ENVS[env]
    if level is not None: b[CFG_OFF + 15] = level
    if strict is not None: b[CFG_OFF + 16] = strict
    if fw is not None: b[CFG_OFF + 18:CFG_OFF + 20] = struct.pack("<H", fw)
    open(outp or path, "wb").write(b)
    print("patched %s: ip %s port %d env %d level %d strict %d fw %d" % (outp or path, socket.inet_ntoa(bytes(b[CFG_OFF + 8:CFG_OFF + 12])),
          struct.unpack(">H", b[CFG_OFF + 12:CFG_OFF + 14])[0], b[CFG_OFF + 14], b[CFG_OFF + 15], b[CFG_OFF + 16],
          struct.unpack("<H", b[CFG_OFF + 18:CFG_OFF + 20])[0]))


def listen(port, outdir):
    os.makedirs(outdir, exist_ok=True)
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port)); s.listen(4)
    print("Ps4HardwareProbe receiver on :%d -> %s" % (port, outdir), flush=True)
    while True:
        c, addr = s.accept()
        name = os.path.join(outdir, "probe-%s" % time.strftime("%Y%m%d-%H%M%S"))
        data = b""
        c.settimeout(600)
        with open(name + ".bin", "wb") as f:
            while True:
                try:
                    chunk = c.recv(65536)
                except socket.timeout:
                    break
                if not chunk:
                    break
                f.write(chunk); f.flush(); data += chunk
        c.close()
        try:
            r = analyse(data)
            txt = report(r)
            open(name + ".txt", "w").write(txt + "\n")
            open(name + ".json", "w").write(json.dumps(jsonable(r), indent=1))
            print("run from %s -> %s.bin\n%s" % (addr[0], name, txt), flush=True)
        except Exception as e:  # keep the raw capture even if decoding fails
            print("run from %s -> %s.bin (decode failed: %s)" % (addr[0], name, e), flush=True)


def jsonable(r):
    j = dict(r)
    j["diag"] = {DIAGS.get(k, str(k)): {"status": STATUS.get(v[0], v[0]), "reason": v[1]} for k, v in r["diag"].items()}
    j["msr"] = {hex(k): hex(v) for k, v in r["msr"].items()}
    return j


def send(path, host, port):
    data = open(path, "rb").read()
    with socket.create_connection((host, port), timeout=10) as s:
        s.sendall(data)
    print("sent %d bytes once to %s:%d" % (len(data), host, port))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("listen"); p.add_argument("--port", type=int, default=9033); p.add_argument("--out", default="reports")
    p = sp.add_parser("decode"); p.add_argument("file"); p.add_argument("--json", action="store_true")
    p = sp.add_parser("compare"); p.add_argument("a"); p.add_argument("b")
    p = sp.add_parser("patch"); p.add_argument("file"); p.add_argument("--ip"); p.add_argument("--port", type=int)
    p.add_argument("--env", choices=sorted(ENVS)); p.add_argument("--level", type=int, choices=(0, 1, 2))
    p.add_argument("--strict", type=int, choices=(0, 1)); p.add_argument("--fw", type=int); p.add_argument("-o")
    p = sp.add_parser("send"); p.add_argument("file"); p.add_argument("--host", required=True); p.add_argument("--port", type=int, default=9090)
    a = ap.parse_args()
    if a.cmd == "listen": listen(a.port, a.out)
    elif a.cmd == "decode":
        r = analyse(open(a.file, "rb").read())
        print(json.dumps(jsonable(r), indent=1) if a.json else report(r))
    elif a.cmd == "compare": print(compare(open(a.a, "rb").read(), open(a.b, "rb").read()))
    elif a.cmd == "patch": patch(a.file, a.ip, a.port, a.env, a.level, a.strict, a.fw, a.o)
    elif a.cmd == "send": send(a.file, a.host, a.port)


if __name__ == "__main__":
    main()
