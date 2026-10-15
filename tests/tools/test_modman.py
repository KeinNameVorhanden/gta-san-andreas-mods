"""Behaviour tests for tools/modman.py on throwaway game folders (python -m unittest discover tests/tools)."""
import io
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import modman  # noqa: E402


def run(game: Path, *args: str) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out):
        try:
            code = modman.main(["--game", str(game), *args])
        except SystemExit as exit_:
            code = exit_.code if isinstance(exit_.code, int) else 1
            out.write(str(exit_.code))
    return code, out.getvalue()


def write_mod(game: Path, mod_id: str, version: str = "1.0.0", deps: str = "", api: int = 1) -> None:
    folder = game / "sacore" / "mods" / mod_id
    folder.mkdir(parents=True)
    (folder / "mod.toml").write_text(
        f'id = "{mod_id}"\nname = "{mod_id}"\nversion = "{version}"\napi_version = {api}\ndll = "{mod_id}.dll"\n'
        + (f"[dependencies]\n{deps}\n" if deps else ""), encoding="utf-8")
    (folder / f"{mod_id}.dll").write_bytes(b"MZ")


class ModmanTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.game = Path(self.temp.name) / "game"
        (self.game / "scripts").mkdir(parents=True)
        (self.game / "gta_sa.exe").write_bytes(b"MZ")
        (self.game / "scripts" / "SACore.asi").write_bytes(b"core")
        (self.game / "scripts" / "SilentPatchSA.asi").write_bytes(b"sp")
        (self.game / "scripts" / "extra").mkdir()
        (self.game / "scripts" / "extra" / "Nested.asi").write_bytes(b"nested")

    def tearDown(self):
        self.temp.cleanup()

    def test_disabled_asi_leaves_the_folders_the_asi_loader_scans_and_comes_back_unchanged(self):
        code, out = run(self.game, "disable", "asi:scripts/extra/Nested.asi")
        self.assertEqual(code, 0)
        self.assertIn("Dry-run", out)
        self.assertTrue((self.game / "scripts" / "extra" / "Nested.asi").exists())
        run(self.game, "--apply", "disable", "asi:scripts/extra/Nested.asi")
        self.assertEqual(list((self.game / "scripts").rglob("Nested.asi")), [])
        self.assertTrue(any((self.game / "sacore" / "backups").rglob("Nested.asi")))
        run(self.game, "--apply", "enable", "asi:scripts/extra/Nested.asi")
        self.assertEqual((self.game / "scripts" / "extra" / "Nested.asi").read_bytes(), b"nested")

    def test_the_framework_itself_is_never_listed_as_a_manageable_plugin(self):
        _, out = run(self.game, "list")
        self.assertNotIn("SACore.asi", out)
        self.assertIn("asi:scripts/SilentPatchSA.asi", out)

    def test_profile_apply_switches_exactly_the_difference(self):
        write_mod(self.game, "alpha")
        write_mod(self.game, "beta")
        run(self.game, "profile", "save", "all")
        run(self.game, "--apply", "disable", "beta")
        run(self.game, "--apply", "disable", "asi:scripts/SilentPatchSA.asi")
        run(self.game, "profile", "save", "minimal")
        run(self.game, "--apply", "profile", "apply", "all")
        _, listing = run(self.game, "list")
        self.assertNotIn("off", listing)
        code, out = run(self.game, "--apply", "profile", "apply", "minimal")
        self.assertEqual(code, 0)
        self.assertEqual(out.count("move "), 2)
        _, listing = run(self.game, "list")
        self.assertIn("off  mod:beta", listing)
        self.assertIn("on   mod:alpha", listing)

    def test_check_reports_disabled_and_mismatching_dependencies(self):
        write_mod(self.game, "lib", "1.4.0")
        write_mod(self.game, "user", deps='lib = "^2.0"')
        write_mod(self.game, "other", deps='helper = "*"')
        write_mod(self.game, "future", api=2)
        code, out = run(self.game, "check")
        self.assertEqual(code, 1)
        self.assertIn("mod:user: requires 'lib' ^2.0, found 1.4.0", out)
        self.assertIn("mod:other: requires 'helper' *, which is not enabled", out)
        self.assertIn("mod:future: built for mod API 2", out)

    def test_install_rejects_unsafe_archives_and_installs_valid_ones(self):
        bad = Path(self.temp.name) / "bad.zip"
        with zipfile.ZipFile(bad, "w") as z:
            z.writestr("mod.toml", 'id = "evil"\nname = "e"\nversion = "1.0"\napi_version = 1\ndll = "e.dll"\n')
            z.writestr("e.dll", b"MZ")
            z.writestr("../escape.txt", b"x")
        code, out = run(self.game, "--apply", "install", str(bad))
        self.assertNotEqual(code, 0)
        self.assertIn("unsafe path", out)
        good = Path(self.temp.name) / "good.zip"
        with zipfile.ZipFile(good, "w") as z:
            z.writestr("good/mod.toml", 'id = "good"\nname = "g"\nversion = "1.0"\napi_version = 1\ndll = "g.dll"\n')
            z.writestr("good/g.dll", b"MZ")
        code, _ = run(self.game, "--apply", "install", str(good))
        self.assertEqual(code, 0)
        self.assertTrue((self.game / "sacore" / "mods" / "good" / "g.dll").is_file())

    def test_bad_update_archive_is_refused_before_the_installed_version_is_touched(self):
        write_mod(self.game, "good")
        before = sorted(p.relative_to(self.game).as_posix() for p in self.game.rglob("*"))
        for member in ("C:evil.txt", "/abs.txt", "sub\\..\\..\\up.txt"):
            bad = Path(self.temp.name) / "update.zip"
            with zipfile.ZipFile(bad, "w") as z:
                z.writestr("mod.toml", 'id = "good"\nname = "g"\nversion = "2.0"\napi_version = 1\ndll = "good.dll"\n')
                z.writestr("good.dll", b"MZ2")
                z.writestr(member, b"x")
            code, out = run(self.game, "--apply", "install", str(bad))
            self.assertNotEqual(code, 0, member)
            self.assertIn("unsafe path", out)
            # No backup move, no partial write: the installed 1.0.0 is exactly as before.
            self.assertEqual(sorted(p.relative_to(self.game).as_posix() for p in self.game.rglob("*")), before)
            self.assertEqual((self.game / "sacore/mods/good/good.dll").read_bytes(), b"MZ")

    def test_content_only_mod_without_dll_installs(self):
        archive = Path(self.temp.name) / "content.zip"
        with zipfile.ZipFile(archive, "w") as z:
            z.writestr("hud/mod.toml", 'id = "hud"\nname = "h"\nversion = "1.0"\napi_version = 1\n')
            z.writestr("hud/content/data/hud.dat", b"data")
        code, out = run(self.game, "--apply", "install", str(archive))
        self.assertEqual(code, 0, out)
        self.assertEqual((self.game / "sacore/mods/hud/content/data/hud.dat").read_bytes(), b"data")

    def test_requirement_grammar_matches_the_core(self):
        self.assertTrue(modman.satisfies((1, 9, 0), "^1.2.3"))
        self.assertFalse(modman.satisfies((2, 0, 0), "^1.2.3"))
        self.assertFalse(modman.satisfies((0, 4, 0), "^0.3.1"))
        self.assertTrue(modman.satisfies((1, 99, 0), ">=1.2, <2"))
        self.assertFalse(modman.satisfies((1, 5, 0), "~1.4"))


if __name__ == "__main__":
    unittest.main()
