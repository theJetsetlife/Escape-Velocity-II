"""Offline structural tests for the generic non-UEFI research payload."""
import hashlib
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE = ROOT / "src" / "native-research"


def execute(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


class NativeBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for exe in ("clang", "ld.lld", "llvm-objcopy", "make", "readelf", "nm"):
            if shutil.which(exe) is None:
                raise unittest.SkipTest(f"missing build tool: {exe}")
        cls.working_directory = tempfile.TemporaryDirectory()
        cls.module = pathlib.Path(cls.working_directory.name)
        for path in MODULE.iterdir():
            if path.suffix in {".c", ".h", ".S", ".ld"} or path.name == "Makefile":
                shutil.copy2(path, cls.module / path.name)
        execute("make", "-C", str(cls.module), "PC_IP=10.0.0.71", "PC_PORT=9031")
        cls.blob = cls.module / "native-research.bin"
        cls.elf = cls.module / "native-research.elf"

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "working_directory"):
            cls.working_directory.cleanup()

    def test_entry_and_patchable_config(self):
        blob = self.blob.read_bytes()
        self.assertGreater(len(blob), 64)
        self.assertEqual(blob[16:24], b"DBDIAGCF")
        self.assertEqual(blob[24:28], bytes([10, 0, 0, 71]))
        self.assertEqual(struct.unpack(">H", blob[28:30])[0], 9031)
        self.assertEqual(blob[30:32], b"\0\0")
        if blob[0] == 0xeb:
            jump_target = 2 + struct.unpack("b", blob[1:2])[0]
        elif blob[0] == 0xe9:
            jump_target = 5 + struct.unpack("<i", blob[1:5])[0]
        else:
            self.fail("expected relative entry branch")
        self.assertGreaterEqual(jump_target, 32)

    def test_elf_entry_and_user_placeholder(self):
        info = execute("readelf", "-h", str(self.elf))
        self.assertIn("Entry point address:               0x0", info)
        symbols = execute("nm", "-n", str(self.elf))
        self.assertRegex(symbols, r"\bresearch_stage_run\b")
        self.assertRegex(symbols, r"\bresearch_stage_note\b")
        self.assertNotRegex(symbols, r"\bsceSbl[A-Za-z]+\b")
        sections = execute("readelf", "-S", str(self.elf))
        self.assertNotRegex(sections, r"\.(?:got|dynamic|rela)(?:\s|\.)")
        self.assertFalse(re.search(r"\b(?:xmm|ymm|zmm)\d", execute("llvm-objdump", "-d", str(self.elf))))

    def test_checksum_matches(self):
        sidecar = (self.module / "native-research.bin.sha256").read_text()
        digest = hashlib.sha256(self.blob.read_bytes()).hexdigest()
        self.assertEqual(sidecar.split()[0], digest)

    def test_extension_explicitly_unimplemented(self):
        source = (self.module / "experiment.c").read_text()
        self.assertIn("return RESEARCH_STAGE_UNIMPLEMENTED;", source)
        for marker in ("research_prepare", "research_execute", "research_finish"):
            self.assertIn(marker, source)
        self.assertIn("RESEARCH_STAGE_UNIMPLEMENTED 0x7ffe", (self.module / "native.h").read_text())


if __name__ == "__main__":
    unittest.main()
