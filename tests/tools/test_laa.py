"""Behaviour tests for tools/laa.py on throwaway installs."""
import hashlib
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import laa  # noqa: E402


def fake_exe(characteristics: int) -> bytes:
    data = bytearray(b"\xcc" * 0x400)
    data[0:2] = b"MZ"
    data[0x3C:0x40] = (0x80).to_bytes(4, "little")
    data[0x80:0x84] = b"PE\0\0"
    data[0x80 + 4 + 18:0x80 + 4 + 20] = characteristics.to_bytes(2, "little")
    return bytes(data)


class LaaToolTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.game = root / "copy"
        self.game.mkdir()
        self.exe = self.game / "gta_sa.exe"
        self.exe.write_bytes(fake_exe(0x010F))
        self.known = root / "known.json"
        self.known.write_text(json.dumps({"builds": [{"id": "test_build", "exe": "gta_sa.exe",
                                                      "sha256": hashlib.sha256(fake_exe(0x010F)).hexdigest()}]}))

    def tearDown(self):
        self.temp.cleanup()

    def run_tool(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            code = laa.main(list(args), known_builds=self.known)
        return code, out.getvalue()

    def test_sets_and_clears_the_flag_with_a_backup_and_keeps_the_build(self):
        code, out = self.run_tool("--game", str(self.game))
        self.assertEqual(code, 0)
        self.assertIn("Dry-run", out)
        self.assertEqual(self.exe.read_bytes(), fake_exe(0x010F))

        code, out = self.run_tool("--game", str(self.game), "--apply")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.exe.read_bytes(), fake_exe(0x012F))
        backups = list((self.game / "sacore" / "install" / "backups").glob("gta_sa.exe.*.before-laa"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), fake_exe(0x010F))

        code, out = self.run_tool("--game", str(self.game), "--apply")  # patched exe is still the known build
        self.assertEqual(code, 0)
        self.assertIn("nothing to do", out)

        code, out = self.run_tool("--game", str(self.game), "--remove", "--apply")
        self.assertEqual(code, 0)
        self.assertEqual(self.exe.read_bytes(), fake_exe(0x010F))

    def test_copy_with_both_file_names_patches_the_exe_the_build_starts(self):
        # Regression: 1.0 US copies keep a gta-sa.exe from the downgrade next to gta_sa.exe; the first name found was
        # patched while the game kept starting the other one with 2 GB.
        spare = self.game / "gta-sa.exe"
        spare.write_bytes(fake_exe(0x010F))
        code, out = self.run_tool("--game", str(self.game), "--apply")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.exe.read_bytes(), fake_exe(0x012F))
        self.assertEqual(spare.read_bytes(), fake_exe(0x010F))

    def test_refuses_unknown_executables_and_steam_folders(self):
        self.exe.write_bytes(fake_exe(0x0102))
        code, out = self.run_tool("--game", str(self.game), "--apply")
        self.assertEqual(code, 2)
        self.assertIn("not a known build", out)
        steam = Path(self.temp.name) / "steamapps" / "common" / "gta"
        steam.mkdir(parents=True)
        (steam / "gta_sa.exe").write_bytes(fake_exe(0x010F))
        code, out = self.run_tool("--game", str(steam), "--apply")
        self.assertEqual(code, 2)
        self.assertIn("Steam install", out)
        self.assertEqual((steam / "gta_sa.exe").read_bytes(), fake_exe(0x010F))


if __name__ == "__main__":
    unittest.main()
