"""Behaviour tests for tools/triage_crash.py: report parsing, exact-binary symbol choice, game names, unsafe zips."""
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import triage_crash  # noqa: E402

# gta_sa.exe of a 1.0 US copy for the test that reads the real executable; skipped when unset.
US10_EXE = Path(os.environ["SACORE_US10_EXE"]) if os.environ.get("SACORE_US10_EXE") else None
SHA = "08292abddb6a59f2314b5a6d250b392dfba8d6a44df0ed029eba43a18e768c50"
REPORT = f"""SACore crash report 2026-10-13 23:00:59

Exception: 0xc0000005 EXCEPTION_ACCESS_VIOLATION
Faulting address: SACore.asi+0x1010
Access: read at 0x00000010 (null or near-null pointer)

Stack (innermost first):
  #0  SACore.asi+0x1010

Stack scan (call-verified return addresses on the stack, innermost first):
  ~0  SACore.asi+0x2004
  ~1  gta_sa.exe+0x219b71
  ~2  KERNEL32.DLL+0x15d49

Registers:
  EAX 00000000 EBX 00000000 ECX 03DD1E00 EDX 00000000

Context:
  SACore 0.9.5 (mod API 1), build us10_hoodlum
  SACore.asi sha256 {SHA}
  session 32 s, 1725 frames, the game was closing (close requested)
  game state 9, area 0
  mod devtools 1.0.0 running

System:
  Windows 10.0 build 26200

Last log lines:
  2026-10-13 23:00:58.000 [WARN] [Mods] something odd
  2026-10-13 23:00:59.000 [INFO] [Core] last line

Minidump: 2026-10-13_23-00-59.dmp
"""


def write_map(path: Path, functions: dict[int, str]) -> None:
    lines = [" SACore", "", " Preferred load address is 10000000", "",
             "  Address         Publics by Value              Rva+Base       Lib:Object", ""]
    for rva, name in sorted(functions.items()):
        lines.append(f" 0001:{rva:08x}       {name:<30} {0x10000000 + rva:08x} f   core.obj")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class TriageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.saved = triage_crash.SYMBOLS, triage_crash.BUILD
        triage_crash.SYMBOLS = self.root / "symbols"
        triage_crash.BUILD = self.root / "build"

    def tearDown(self):
        triage_crash.SYMBOLS, triage_crash.BUILD = self.saved
        self.temp.cleanup()

    def test_report_fields_are_parsed_from_the_real_format(self):
        r = triage_crash.parse_report(REPORT)
        self.assertEqual(r.fault, "SACore.asi+0x1010")
        self.assertEqual(r.scan, ["SACore.asi+0x2004", "gta_sa.exe+0x219b71", "KERNEL32.DLL+0x15d49"])
        self.assertEqual((r.build, r.core_sha), ("us10_hoodlum", SHA))
        self.assertEqual(r.log[-1], "2026-10-13 23:00:59.000 [INFO] [Core] last line")

    def test_the_map_of_the_crashed_binary_wins_over_a_newer_build(self):
        # The current build moved functions; only the archived map of the crashed binary names them right.
        write_map(triage_crash.SYMBOLS / SHA[:8] / "SACore.map", {0x1000: "?CrashFilter", 0x2000: "?OnFrame"})
        write_map(triage_crash.BUILD / "src" / "core" / "Release" / "SACore.map", {0x1000: "?Newer", 0x3000: "?X"})
        resolver = triage_crash.Resolver(triage_crash.parse_report(REPORT), None)
        self.assertIn("?CrashFilter+0x10", resolver.name("SACore.asi+0x1010"))
        self.assertIn("?OnFrame+0x4", resolver.name("SACore.asi+0x2004"))

    def test_without_the_archived_map_a_different_current_build_is_flagged(self):
        current = triage_crash.BUILD / "src" / "core" / "Release"
        write_map(current / "SACore.map", {0x1000: "?Newer"})
        (current / "SACore.asi").write_bytes(b"a newer build")
        path, note = triage_crash.find_map("SACore.asi", SHA)
        self.assertEqual(path, current / "SACore.map")
        self.assertIn("DIFFERENT binary", note)

    @unittest.skipUnless(US10_EXE and US10_EXE.is_file(), "needs SACORE_US10_EXE: gta_sa.exe of a 1.0 US copy")
    def test_game_frames_get_names_from_the_real_executable(self):
        resolver = triage_crash.Resolver(triage_crash.parse_report(REPORT), US10_EXE)
        self.assertIn("RsEventHandler+0x11", resolver.name("gta_sa.exe+0x219b71"))
        self.assertIn("RwFreeListFree+0x8", resolver.name("gta_sa.exe+0x401d58"))
        # The exit crash call chain: the frame destructor is named by SACore's own analysis.
        self.assertIn("RpHAnim frame destructor", resolver.name("gta_sa.exe+0x3c4806"))

    def test_package_with_an_escaping_path_is_refused(self):
        bad = self.root / "bad.zip"
        with zipfile.ZipFile(bad, "w") as z:
            z.writestr("report.txt", REPORT)
            z.writestr("../../outside.txt", "x")
        with self.assertRaises(SystemExit):
            triage_crash.triage(bad, None)


if __name__ == "__main__":
    unittest.main()
