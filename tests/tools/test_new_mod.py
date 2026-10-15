"""Behaviour tests for tools/new_mod.py (generates into a temporary copy of the repo layout)."""
import io
import shutil
import sys
import tempfile
import tomllib
import unittest
from contextlib import redirect_stdout
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
import new_mod  # noqa: E402


class NewModTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        shutil.copytree(REPO / "templates", root / "templates")
        (root / "tools" / "data").mkdir(parents=True)
        shutil.copy(REPO / "tools" / "data" / "known_builds.json", root / "tools" / "data")
        (root / "VERSION").write_text("1.2.3\n")
        (root / "mods").mkdir()
        self.root = root
        self.saved = new_mod.REPO, new_mod.TEMPLATE
        new_mod.REPO, new_mod.TEMPLATE = root, root / "templates" / "mod"

    def tearDown(self):
        new_mod.REPO, new_mod.TEMPLATE = self.saved
        self.temp.cleanup()

    def run_tool(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            code = new_mod.main(list(args))
        return code, out.getvalue()

    def test_generated_project_has_no_placeholders_and_a_valid_manifest(self):
        code, _ = self.run_tool("hud_tweaks", "--name", 'My "HUD"', "--builds", "us10_hoodlum")
        self.assertEqual(code, 0)
        folder = self.root / "mods" / "hud_tweaks"
        for file in folder.rglob("*"):
            if file.is_file():
                self.assertNotIn("{{", file.read_text(encoding="utf-8"), file)
        manifest = tomllib.loads((folder / "mod.toml").read_text(encoding="utf-8"))
        self.assertEqual(manifest["dll"], "hud_tweaks.dll")
        self.assertEqual(manifest["min_core_version"], "1.2.3")
        self.assertEqual(manifest["target_builds"], ["us10_hoodlum"])
        self.assertTrue((folder / "src" / "hud_tweaks.cpp").is_file())

    def test_refuses_bad_ids_unknown_builds_and_existing_folders(self):
        self.assertEqual(self.run_tool("Bad-Id", "--name", "x")[0], 2)
        self.assertEqual(self.run_tool("ok_id", "--name", "x", "--builds", "ps2")[0], 2)
        (self.root / "mods" / "taken").mkdir()
        code, out = self.run_tool("taken", "--name", "x")
        self.assertEqual(code, 2)
        self.assertIn("already exists", out)


if __name__ == "__main__":
    unittest.main()
