"""Behaviour tests for the release installer (tools/release/installer.py) on throwaway game folders."""
import hashlib
import io
import json
import sys
import tempfile
import tomllib
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "release"))
import installer  # noqa: E402

US10_EXE = b"MZ fake 1.0 US"
STEAM_EXE = b"MZ fake newsteam"


def snapshot(root: Path) -> dict[str, bytes]:
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file()}


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        base = Path(self.temp.name)
        self.game = base / "play" / "us10"
        (self.game / "scripts").mkdir(parents=True)
        (self.game / "gta_sa.exe").write_bytes(US10_EXE)
        self.v1 = self.make_package(base / "pkg1", "1.0.0", {"dinput8.dll": b"ual", "scripts/SACore.asi": b"core 1",
                                                              "sacore/mods/devtools/devtools.dll": b"dev 1"})
        self.v2 = self.make_package(base / "pkg2", "1.1.0", {"dinput8.dll": b"ual", "scripts/SACore.asi": b"core 2",
                                                              "scripts/SACore.ini": b"new in 2",
                                                              "sacore/mods/devtools/devtools.dll": b"dev 2"})

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def make_package(root: Path, version: str, files: dict[str, bytes]) -> Path:
        entries = []
        for rel, data in files.items():
            target = root / "payload" / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            group = "devtools" if "devtools" in rel else "base"
            entries.append({"path": rel, "sha256": hashlib.sha256(data).hexdigest(), "group": group})
        manifest = {"version": version, "optional_groups": ["devtools"], "files": entries,
                    "known_builds": [{"id": "us10_hoodlum", "exe": "gta_sa.exe",
                                      "sha256": hashlib.sha256(US10_EXE).hexdigest()},
                                     {"id": "newsteam_r2_lv", "exe": "gta-sa.exe",
                                      "sha256": hashlib.sha256(STEAM_EXE).hexdigest()}]}
        (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        return root

    def run_installer(self, package: Path, *args: str, game: Path | None = None) -> tuple[int, str]:
        out = io.StringIO()
        with redirect_stdout(out):
            code = installer.main(["--game", str(game or self.game), *args], package=package)
        return code, out.getvalue()

    def test_dry_run_changes_nothing_and_apply_does_what_it_printed(self):
        before = snapshot(self.game)
        code, text = self.run_installer(self.v1, "install", "--with", "devtools")
        self.assertEqual(code, 0)
        self.assertEqual(snapshot(self.game), before)
        self.assertIn("add      scripts/SACore.asi", text)
        code, _ = self.run_installer(self.v1, "--apply", "install", "--with", "devtools")
        self.assertEqual(code, 0)
        self.assertEqual((self.game / "sacore/mods/devtools/devtools.dll").read_bytes(), b"dev 1")
        # 1.0 US gets its own User Files folder; the newsteam build would share Documents with the Steam game.
        config = tomllib.loads((self.game / "sacore/config/core.toml").read_text(encoding="utf-8"))
        self.assertTrue(config["saves"]["separate_user_files"])
        self.assertEqual(self.run_installer(self.v1, "verify")[0], 0)

    def test_foreign_loader_is_backed_up_and_returned_by_uninstall_while_saves_stay(self):
        (self.game / "dinput8.dll").write_bytes(b"someone else's loader")
        self.run_installer(self.v1, "--apply", "install")
        self.assertEqual((self.game / "dinput8.dll").read_bytes(), b"ual")
        save = self.game / "sacore/userdata/GTA San Andreas User Files/GTASAsf1.b"
        save.parent.mkdir(parents=True)
        save.write_bytes(b"save")
        code, text = self.run_installer(self.v1, "--apply", "uninstall")
        self.assertEqual(code, 0, text)
        self.assertEqual((self.game / "dinput8.dll").read_bytes(), b"someone else's loader")
        self.assertFalse((self.game / "scripts/SACore.asi").exists())
        self.assertEqual(save.read_bytes(), b"save")
        self.assertTrue((self.game / "sacore/config/core.toml").is_file())
        # Nothing was deleted: the removed framework file sits in the uninstall backup.
        removed = list((self.game / "sacore/install/backups").glob("*-uninstall/scripts/SACore.asi"))
        self.assertEqual([p.read_bytes() for p in removed], [b"core 1"])

    def test_file_changed_by_the_user_is_reported_by_verify_and_kept_by_uninstall(self):
        self.run_installer(self.v1, "--apply", "install")
        (self.game / "scripts/SACore.asi").write_bytes(b"patched by hand")
        code, text = self.run_installer(self.v1, "verify")
        self.assertEqual(code, 1)
        self.assertIn("CHANGED  scripts/SACore.asi", text)
        self.run_installer(self.v1, "--apply", "uninstall")
        self.assertEqual((self.game / "scripts/SACore.asi").read_bytes(), b"patched by hand")

    def test_update_then_rollback_restores_the_previous_version_exactly(self):
        self.run_installer(self.v1, "--apply", "install", "--with", "devtools")
        installed_v1 = snapshot(self.game)
        code, text = self.run_installer(self.v2, "--apply", "update")
        self.assertEqual(code, 0, text)
        self.assertEqual((self.game / "scripts/SACore.asi").read_bytes(), b"core 2")
        self.assertEqual((self.game / "sacore/mods/devtools/devtools.dll").read_bytes(), b"dev 2")  # group kept
        self.assertNotIn("add      dinput8.dll", text)
        code, text = self.run_installer(self.v2, "--apply", "recover", "rollback")
        self.assertEqual(code, 0, text)
        after = {k: v for k, v in snapshot(self.game).items() if not k.startswith("sacore/install/")}
        self.assertEqual(after, {k: v for k, v in installed_v1.items() if not k.startswith("sacore/install/")})
        self.assertEqual(self.run_installer(self.v1, "verify")[0], 0)

    def test_refusals_steam_folder_unknown_build_install_twice(self):
        steam = Path(self.temp.name) / "SteamLibrary" / "steamapps" / "common" / "GTA SA"
        steam.mkdir(parents=True)
        (steam / "gta-sa.exe").write_bytes(STEAM_EXE)
        code, text = self.run_installer(self.v1, "--apply", "install", game=steam)
        self.assertEqual(code, 2)
        self.assertEqual(snapshot(steam), {"gta-sa.exe": STEAM_EXE})
        (self.game / "gta_sa.exe").write_bytes(b"MZ modified exe")
        self.assertEqual(self.run_installer(self.v1, "--apply", "install")[0], 2)
        self.assertEqual(self.run_installer(self.v1, "--apply", "--force-unknown-build", "install")[0], 0)
        self.assertEqual(self.run_installer(self.v1, "--apply", "--force-unknown-build", "install")[0], 2)

    def test_a_copy_with_the_4gb_flag_is_still_its_build(self):
        exe = bytearray(b"\xcc" * 0x200)
        exe[0:2] = b"MZ"
        exe[0x3C:0x40] = (0x80).to_bytes(4, "little")
        exe[0x80:0x84] = b"PE\0\0"
        exe[0x80 + 22] = 0x0F
        package = self.make_package(Path(self.temp.name) / "pkg3", "1.0.0", {"scripts/SACore.asi": b"core"})
        manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
        manifest["known_builds"][0]["sha256"] = hashlib.sha256(bytes(exe)).hexdigest()
        (package / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        exe[0x80 + 22] |= 0x20  # Large Address Aware (tools/laa.py)
        (self.game / "gta_sa.exe").write_bytes(bytes(exe))
        code, text = self.run_installer(package, "--apply", "install")
        self.assertEqual(code, 0, text)
        exe[0x150] ^= 1  # any other change is a different file
        (self.game / "gta_sa.exe").write_bytes(bytes(exe))
        self.assertIsNone(installer.Installer(package, self.game, False, out=lambda *_: None).build_id())

    def test_safe_mode_switches_only_mods_enabled(self):
        self.run_installer(self.v1, "--apply", "install")
        config = self.game / "sacore/config/core.toml"
        config.write_text("[mods]\n# Load mods\nenabled = true\n\n[window]\nenabled = true\nwindowed = true\n",
                          encoding="utf-8")
        self.run_installer(self.v1, "--apply", "recover", "safe-mode")
        data = tomllib.loads(config.read_text(encoding="utf-8"))
        self.assertEqual(data, {"mods": {"enabled": False}, "window": {"enabled": True, "windowed": True}})
        self.run_installer(self.v1, "--apply", "recover", "normal")
        self.assertTrue(tomllib.loads(config.read_text(encoding="utf-8"))["mods"]["enabled"])


if __name__ == "__main__":
    unittest.main()
