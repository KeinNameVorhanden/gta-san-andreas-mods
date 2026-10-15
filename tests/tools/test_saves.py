"""Behaviour tests for tools/saves.py on throwaway installs."""
import io
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import saves  # noqa: E402


def make_save(fill: int) -> bytes:
    data = bytearray([fill]) * saves.SAVE_SIZE
    data[:5] = b"BLOCK"
    struct.pack_into("<I", data, saves.CHECKSUM_OFFSET, sum(data[:saves.CHECKSUM_OFFSET]) & 0xFFFFFFFF)
    return bytes(data)


def run(*args: str) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out):
        try:
            code = saves.main(list(args))
        except SystemExit as exit_:
            code = 1
            out.write(str(exit_.code))
    return code, out.getvalue()


class SavesToolTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.game = Path(self.temp.name)
        (self.game / "sacore" / "config").mkdir(parents=True)
        (self.game / "sacore" / "config" / "core.toml").write_text("[saves]\nseparate_user_files = true\n")
        self.folder = self.game / "sacore" / "userdata" / "GTA San Andreas User Files"
        self.folder.mkdir(parents=True)
        self.backups = self.game / "sacore" / "save-backups" / "GTASAsf2.b"
        self.backups.mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def make_install(self, name: str, exe: bytes) -> tuple[Path, Path]:
        root = Path(self.temp.name) / name
        (root / "sacore" / "config").mkdir(parents=True)
        (root / "sacore" / "config" / "core.toml").write_text("[saves]\nseparate_user_files = true\n")
        (root / "gta-sa.exe").write_bytes(exe)
        folder = root / "sacore" / "userdata" / "GTA San Andreas User Files"
        folder.mkdir(parents=True)
        return root, folder

    def test_story_copy_takes_valid_saves_of_the_same_build_only(self):
        source, source_folder = self.make_install("played", b"MZ build A")
        target, target_folder = self.make_install("mine", b"MZ build A")
        other, _ = self.make_install("other", b"MZ build B")
        (source_folder / "GTASAst1.b").write_bytes(make_save(0x11))
        (source_folder / "GTASAst12.b").write_bytes(make_save(0x12))
        damaged = bytearray(make_save(0x13))
        damaged[200] ^= 1
        (source_folder / "GTASAst13.b").write_bytes(bytes(damaged))
        (source_folder / "GTASAsf1.b").write_bytes(make_save(0x14))  # the player's own save stays where it is
        (target_folder / "GTASAst1.b").write_bytes(make_save(0x01))

        code, out = run("--game", str(other), "--apply", "story-copy", "--from", str(source))
        self.assertNotEqual(code, 0)
        self.assertIn("not the same game build", out)

        code, out = run("--game", str(target), "story-copy", "--from", str(source))
        self.assertEqual(code, 0)
        self.assertIn("Dry-run", out)
        self.assertEqual((target_folder / "GTASAst1.b").read_bytes(), make_save(0x01))

        code, out = run("--game", str(target), "--apply", "story-copy", "--from", str(source))
        self.assertEqual(code, 0)
        self.assertIn("skip GTASAst13.b: damaged", out)
        self.assertEqual((target_folder / "GTASAst12.b").read_bytes(), make_save(0x12))
        self.assertEqual((target_folder / "GTASAst1.b").read_bytes(), make_save(0x11))
        self.assertFalse((target_folder / "GTASAst13.b").exists())
        self.assertFalse((target_folder / "GTASAsf1.b").exists())
        kept = list((target / "sacore" / "save-backups" / "GTASAst1.b").glob("*.before-copy.b"))
        self.assertEqual(len(kept), 1)
        self.assertEqual(kept[0].read_bytes(), make_save(0x01))

    def test_restore_keeps_the_current_file_and_refuses_damaged_backups(self):
        (self.folder / "GTASAsf2.b").write_bytes(make_save(0x02))
        (self.backups / "2026-10-13_10-00-00.b").write_bytes(make_save(0x01))
        damaged = bytearray(make_save(0x03))
        damaged[100] ^= 1
        (self.backups / "2026-10-13_11-00-00.b").write_bytes(bytes(damaged))

        code, out = run("--game", str(self.game), "--apply", "restore", "2", "2026-10-13_11-00-00.b")
        self.assertNotEqual(code, 0)
        self.assertIn("refusing to restore a damaged", out)

        code, _ = run("--game", str(self.game), "--apply", "restore", "2", "2026-10-13_10-00-00.b")
        self.assertEqual(code, 0)
        self.assertEqual((self.folder / "GTASAsf2.b").read_bytes(), make_save(0x01))
        kept = list(self.backups.glob("*.before-restore.b"))
        self.assertEqual(len(kept), 1)
        self.assertEqual(kept[0].read_bytes(), make_save(0x02))

    def test_backup_names_cannot_escape_the_slot_folder(self):
        code, out = run("--game", str(self.game), "restore", "2", "../GTASAsf3.b/x.b")
        self.assertNotEqual(code, 0)
        self.assertIn("no backup", out)

    def test_list_reports_damage(self):
        bad = bytearray(make_save(0x05))
        bad[-1] ^= 0xFF
        (self.folder / "GTASAsf4.b").write_bytes(bytes(bad))
        _, out = run("--game", str(self.game), "list")
        self.assertIn("slot 4: damaged: checksum", out)


if __name__ == "__main__":
    unittest.main()
