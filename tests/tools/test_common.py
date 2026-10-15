"""Behaviour tests for the game-agnostic workshop utilities (tools/common)."""
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
from common import detect, pe  # noqa: E402

BUILT_CORE = REPO / "build" / "x86" / "src" / "core" / "Release" / "SACore.asi"


class PeTest(unittest.TestCase):
    @unittest.skipUnless(BUILT_CORE.is_file(), "needs a Release build of SACore.asi")
    def test_reads_machine_aslr_imports_and_pdb_of_a_real_binary(self):
        info = pe.read_pe(BUILT_CORE)
        self.assertEqual(info.machine, "x86")
        self.assertTrue(info.aslr)
        self.assertIn("kernel32.dll", info.imports)
        self.assertIn("user32.dll", info.imports)
        self.assertTrue(info.pdb_path.lower().endswith("sacore.pdb"), info.pdb_path)
        self.assertIn(".text", info.sections)

    @unittest.skipUnless(BUILT_CORE.is_file(), "needs a Release build of SACore.asi")
    def test_truncated_file_is_reported_not_crashed(self):
        with tempfile.TemporaryDirectory() as temp:
            cut = Path(temp) / "cut.exe"
            cut.write_bytes(BUILT_CORE.read_bytes()[:0x120])
            with self.assertRaises(pe.NotPe):
                pe.read_pe(cut)


class DetectTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_proxy_dll_is_a_loader_only_with_its_marker(self):
        # GTA SA ships a real vorbisFile.dll; only the dinput8.dll carrying the ASI loader marker counts.
        (self.root / "game.exe").write_bytes(b"MZ not really a PE")
        (self.root / "vorbisFile.dll").write_bytes(b"MZ ogg vorbis decoder")
        (self.root / "dinput8.dll").write_bytes(b"MZ ... IsUltimateASILoader ...")
        report = detect.detect(self.root)
        loaders = [(f.what, f.evidence) for f in report.loaders]
        self.assertEqual(loaders, [("Ultimate ASI Loader", "dinput8.dll contains 'IsUltimateASILoader'")])
        self.assertTrue(any("game.exe" in p for p in report.problems))  # unreadable PE is reported, not raised

    def test_two_asi_loader_copies_are_flagged(self):
        (self.root / "dinput8.dll").write_bytes(b"IsUltimateASILoader")
        (self.root / "winmm.dll").write_bytes(b"IsUltimateASILoader")
        report = detect.detect(self.root)
        self.assertTrue(any("more than one ASI loader" in p for p in report.problems))

    def test_unity_layout_is_recognised_and_an_empty_folder_stays_unknown(self):
        (self.root / "Game.exe").write_bytes(b"MZ")
        (self.root / "Game_Data").mkdir()
        self.assertIn("Unity", [f.what for f in detect.detect(self.root).engine])
        empty = self.root / "empty"
        empty.mkdir()
        report = detect.detect(empty)
        self.assertEqual([f.what for f in report.engine], ["unknown"])
        self.assertIn("no executable found", report.problems)


if __name__ == "__main__":
    unittest.main()
