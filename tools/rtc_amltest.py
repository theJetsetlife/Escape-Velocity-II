#!/usr/bin/env python3
"""rtc_amltest.py - run the DSDT's ACPI Time and Alarm Device (\\_SB.PTAD) in ACPICA's acpiexec.

A generated SSDT fills the mailbox and counter fields (simulated memory: the placeholders are not patched),
calls _STA/_GRT/_SRT and compares every field with Python's calendar. Needs iasl + acpiexec.
  python3 -I tools/rtc_amltest.py
"""
import datetime, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ASL = os.path.join(HERE, "..", "Ps4Pkg", "Ps4AcpiDxe", "Ps4Dsdt.asl")
EPOCH = 1325376000
U64 = (1 << 64) - 1

def civil(unix):
    d = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=unix)
    return d.year, d.month, d.day, d.hour, d.minute, d.second

# (counter, offset): Fat-like values, leap days, century rules, 32-bit counter wrap, negative offsets
cases = []
for unix in [1325376000, 1330473599, 1330473600, 1330559999, 1735689599, 1735689600, 1795000000,
             1798761599, 4102444799, 4102444800, 4107542399, 4107542400, 1609459199, 951782400, 978307199]:
    for counter in [0, 1, 123456789, 0xFFFFFFFF]:
        cases.append((counter, (unix - EPOCH - counter) & U64, unix))
srt = [(2024, 2, 29, 23, 59, 59), (2000, 3, 1, 0, 0, 0), (2100, 2, 28, 12, 34, 56), (2026, 11, 20, 18, 0, 0),
       (1970, 1, 1, 0, 0, 0), (9999, 12, 31, 23, 59, 59)]
bad = [(1969, 12, 31, 0, 0, 0), (2026, 13, 1, 0, 0, 0), (2026, 1, 0, 0, 0, 0), (2026, 1, 1, 24, 0, 0)]
# (timezone word, daylight): rejected by _SRT like Ps4SetTime; and accepted edge values
badtz = [(0x05A1, 0), (0xFA5F, 0), (0x7FFF, 0), (0x07FE, 0), (0x0000, 0x04), (0x07FF, 0x80)]
goodtz = [(0x05A0, 3), (0xFA60, 0), (0xFE20, 1), (0x07FF, 2)]   # +1440, -1440, -480, unspecified

def setcnt(c):
    return "".join("\\_SB.PTAD.CB%d = 0x%02X\n" % (i, (c >> (8 * i)) & 0xFF) for i in range(4))

def chk(tag, exp, tz=2047, dl=0):
    y, mo, d, h, mi, s = exp
    conds = [("YY", y), ("MO", mo), ("DD", d), ("HH", h), ("MI", mi), ("SS", s), ("VV", 1), ("TZ", tz), ("DL", dl)]
    lines = ["T0 = \\_SB.PTAD._GRT ()"]
    for name, off, w in [("YY", 0, 2), ("MO", 2, 1), ("DD", 3, 1), ("HH", 4, 1), ("MI", 5, 1), ("SS", 6, 1),
                         ("VV", 7, 1), ("TZ", 10, 2), ("DL", 12, 1)]:
        v = dict(conds)[name]
        if w == 2:
            lines.append("If ((DerefOf (T0 [%d]) | (DerefOf (T0 [%d]) << 8)) != 0x%X) { FAIL++ ; Debug = \"%s %s\" }" % (off, off + 1, v, tag, name))
        else:
            lines.append("If (DerefOf (T0 [%d]) != 0x%X) { FAIL++ ; Debug = \"%s %s\" }" % (off, v, tag, name))
    return "\n".join(lines) + "\n"

body = ["\\_SB.PTAD.MSIG = 0x54523450", "\\_SB.PTAD.MFLG = One", "\\_SB.PTAD.MTZ = 2047", "\\_SB.PTAD.MDL = Zero",
        "If (\\_SB.PTAD._STA () != 0x0F) { FAIL++ ; Debug = \"STA\" }",
        "If (\\_SB.PTAD._GCP () != 0x04) { FAIL++ ; Debug = \"GCP\" }"]
n = 0
for c, off, unix in cases:
    body.append(setcnt(c) + "\\_SB.PTAD.MOFS = 0x%016X" % off)
    body.append(chk("GRT%d" % n, civil(unix))); n += 1
for i, t in enumerate(srt):
    counter = 0x0789ABCD
    body.append(setcnt(counter))
    body.append("T1 = Buffer (0x10) { 0x%02X, 0x%02X, %d, %d, %d, %d, %d, 0, 0, 0, 0x3C, 0x00, 1, 0, 0, 0 }"
                % (t[0] & 0xFF, t[0] >> 8, *t[1:]))
    body.append("If (\\_SB.PTAD._SRT (T1) != Zero) { FAIL++ ; Debug = \"SRT%d rc\" }" % i)
    body.append(chk("SRT%d" % i, t, tz=0x3C, dl=1))
    unix = int((datetime.datetime(*t) - datetime.datetime(1970, 1, 1)).total_seconds())
    body.append("If (\\_SB.PTAD.MOFS != 0x%016X) { FAIL++ ; Debug = \"SRT%d ofs\" }" % ((unix - EPOCH - counter) & U64, i))
    body.append("\\_SB.PTAD.MTZ = 2047\n\\_SB.PTAD.MDL = Zero")
for i, t in enumerate(bad):
    body.append("T1 = Buffer (0x10) { 0x%02X, 0x%02X, %d, %d, %d, %d, %d, 0, 0, 0, 0, 0, 0, 0, 0, 0 }"
                % (t[0] & 0xFF, t[0] >> 8, *t[1:]))
    body.append("If (\\_SB.PTAD._SRT (T1) != 0xFFFFFFFF) { FAIL++ ; Debug = \"BAD%d\" }" % i)
def tbuf(t, tz, dl):
    return ("T1 = Buffer (0x10) { 0x%02X, 0x%02X, %d, %d, %d, %d, %d, 0, 0, 0, 0x%02X, 0x%02X, 0x%02X, 0, 0, 0 }"
            % (t[0] & 0xFF, t[0] >> 8, *t[1:], tz & 0xFF, tz >> 8, dl))
okt = (2026, 11, 20, 18, 0, 0)
body.append(setcnt(0x0789ABCD) + "\\_SB.PTAD.MOFS = 0x1234")
for i, (tz, dl) in enumerate(badtz):
    body.append(tbuf(okt, tz, dl))
    body.append("If (\\_SB.PTAD._SRT (T1) != 0xFFFFFFFF) { FAIL++ ; Debug = \"BADTZ%d\" }" % i)
    body.append("If ((\\_SB.PTAD.MOFS != 0x1234) || (\\_SB.PTAD.MTZ != 2047)) { FAIL++ ; Debug = \"BADTZ%d kept\" }" % i)
for i, (tz, dl) in enumerate(goodtz):
    body.append(tbuf(okt, tz, dl))
    body.append("If (\\_SB.PTAD._SRT (T1) != Zero) { FAIL++ ; Debug = \"TZ%d rc\" }" % i)
    body.append(chk("TZ%d" % i, okt, tz=tz, dl=dl))
    body.append("\\_SB.PTAD.MTZ = 2047\n\\_SB.PTAD.MDL = Zero\n\\_SB.PTAD.MOFS = 0x1234")
# a register reading all ones (function no longer decoding): _GRT Valid = 0, _SRT fails, Offset kept
for reg in range(4):
    body.append(setcnt(0x0789ABCD) + "\\_SB.PTAD.CB%d = 0xFFFFFFFF" % reg)
    body.append("T0 = \\_SB.PTAD._GRT ()\nIf (DerefOf (T0 [7]) != Zero) { FAIL++ ; Debug = \"DEAD%d GRT\" }" % reg)
    body.append(tbuf(okt, 0x07FF, 0))
    body.append("If (\\_SB.PTAD._SRT (T1) != 0xFFFFFFFF) { FAIL++ ; Debug = \"DEAD%d SRT\" }" % reg)
    body.append("If (\\_SB.PTAD.MOFS != 0x1234) { FAIL++ ; Debug = \"DEAD%d kept\" }" % reg)
body.append("\\_SB.PTAD.MFLG = Zero\nIf (\\_SB.PTAD._STA () != Zero) { FAIL++ ; Debug = \"STA0\" }")

ssdt = """DefinitionBlock ("", "SSDT", 2, "PS4FW ", "RTCTEST ", 1)
{
    External (\\_SB.PTAD, DeviceObj)
    External (\\_SB.PTAD._STA, MethodObj)
    External (\\_SB.PTAD._GCP, MethodObj)
    External (\\_SB.PTAD._GRT, MethodObj)
    External (\\_SB.PTAD._SRT, MethodObj)
    External (\\_SB.PTAD.MSIG, FieldUnitObj)
    External (\\_SB.PTAD.MFLG, FieldUnitObj)
    External (\\_SB.PTAD.MOFS, FieldUnitObj)
    External (\\_SB.PTAD.MTZ, FieldUnitObj)
    External (\\_SB.PTAD.MDL, FieldUnitObj)
    External (\\_SB.PTAD.CB0, FieldUnitObj)
    External (\\_SB.PTAD.CB1, FieldUnitObj)
    External (\\_SB.PTAD.CB2, FieldUnitObj)
    External (\\_SB.PTAD.CB3, FieldUnitObj)
    Name (FAIL, Zero)
    Name (T0, Buffer (0x10) {})
    Name (T1, Buffer (0x10) {})
    Method (TRTC, 0, Serialized)
    {
%s
        Return (FAIL)
    }
}
""" % "\n".join("        " + l for b in body for l in b.splitlines())

with tempfile.TemporaryDirectory() as t:
    open(os.path.join(t, "t.asl"), "w").write(ssdt)
    for src, out in ((ASL, "dsdt"), (os.path.join(t, "t.asl"), "ssdt")):
        r = subprocess.run(["iasl", "-p", os.path.join(t, out), src], capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout, r.stderr); sys.exit("iasl failed: " + out)
    r = subprocess.run(["acpiexec", "-b", "evaluate \\TRTC", os.path.join(t, "dsdt.aml"), os.path.join(t, "ssdt.aml")],
                       capture_output=True, text=True, timeout=300)
    log = r.stdout + r.stderr
    res = [l for l in log.splitlines() if "Evaluation of" in l or "Integer" in l or "Debug" in l or "Error" in l or "AE_" in l]
    print("\n".join(res[-30:]))
    ok = any("Integer" in l and "0000000000000000" in l for l in res) and "Evaluation of \\TRTC returned" in log
    print("cases: %d _GRT, %d _SRT, %d rejected dates, %d rejected / %d accepted timezones, 4 dead registers"
          % (len(cases), len(srt), len(bad), len(badtz), len(goodtz)))
    print("RESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
