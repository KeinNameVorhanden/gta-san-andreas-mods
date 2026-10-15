"""Behaviour tests for the session merge in tools/run_runtime_tests.py (several scenarios, one game start)."""
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import run_runtime_tests as runner  # noqa: E402


class SessionMergeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def scenario(self, name: str, text: str) -> Path:
        path = self.folder / f"{name}.toml"
        path.write_text(text, encoding="utf-8")
        return path

    def test_steps_run_in_a_row_with_one_quit_and_results_map_back(self):
        a = self.scenario("a", 'name = "a"\nexpect_log_after_exit = ["bye a"]\n[mod_config.x]\nk = 1\n'
                               '[[step]]\naction = "wait_state"\nvalue = 9\n[[step]]\naction = "quit"\n')
        b = self.scenario("b", 'name = "b"\n[mod_config.x]\nk = 1\n[mod_config.y]\nz = "q\\"t"\n'
                               '[[step]]\naction = "set_money"\nvalue = 5\n'
                               '[[step]]\naction = "expect_log"\ncontains = "C:\\\\x"\n[[step]]\naction = "quit"\n')
        merged, ranges = runner.merge_session("s", [a, b], self.folder / "out")
        data = tomllib.loads(merged.read_text(encoding="utf-8"))
        self.assertEqual([s["action"] for s in data["step"]], ["wait_state", "set_money", "expect_log", "quit"])
        self.assertEqual(data["step"][2]["contains"], "C:\\x")
        self.assertEqual(data["mod_config"], {"x": {"k": 1}, "y": {"z": 'q"t'}})
        self.assertEqual(data["expect_log_after_exit"], ["bye a"])
        self.assertEqual(ranges, [("a", 1, 1), ("b", 2, 3)])

        outcome = {"passed": False, "exit_code": 0, "detail": "x",
                   "results": {"steps": [{"index": 1, "action": "wait_state", "passed": True, "message": ""},
                                         {"index": 2, "action": "set_money", "passed": False, "message": "no"}]}}
        parts = runner.split_session(outcome, ranges)
        self.assertTrue(parts[0]["passed"])
        self.assertFalse(parts[1]["passed"])
        self.assertIn("step 1 set_money: no", parts[1]["detail"])

    def test_scenarios_that_cannot_share_a_game_stay_apart(self):
        a = self.scenario("a", 'name = "a"\n[mod_config.x]\nk = 1\n[[step]]\naction = "quit"\n')
        b = self.scenario("b", 'name = "b"\n[mod_config.x]\nk = 2\n[[step]]\naction = "quit"\n')
        c = self.scenario("c", 'name = "c"\n[config.saves]\nseparate_user_files = true\n[[step]]\naction = "quit"\n')
        d = self.scenario("d", 'name = "d"\nexpect_crash = true\n[[step]]\naction = "quit"\n')
        self.assertIsNone(runner.merge_session("s", [a, b], self.folder))  # conflicting mod setting
        self.assertIsNone(runner.merge_session("s", [a, c], self.folder))  # different core config
        self.assertIsNone(runner.merge_session("s", [a, d], self.folder))  # a crash ends the game


class ConfigJournalTest(unittest.TestCase):
    """Regression: a killed run left run_to = "Monster", load_story_saves = false and auto_skip = true behind, and the
    next runs started from those settings."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.game = Path(self.temp.name) / "copy"
        self.mods = self.game / "sacore" / "config" / "mods"
        self.mods.mkdir(parents=True)
        self.testing = self.game / "sacore" / "testing"
        self.testing.mkdir()
        (self.game / "sacore" / "config" / "core.toml").write_text("[mods]\nenabled = true\n", encoding="utf-8")
        (self.mods / "mission_pass.toml").write_text('run_to = "off"\n', encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_a_killed_run_is_undone_at_the_next_start(self):
        new_file = self.mods / "cutscene_skip.toml"  # created by a scenario's [mod_config]
        snapshot = runner.snapshot_configs(self.game, [new_file])
        (self.testing / runner.CONFIG_JOURNAL).write_text(runner.json.dumps(snapshot), encoding="utf-8")
        # What the killed run left: a set_setting step, a [mod_config] file, a [config] override.
        (self.mods / "mission_pass.toml").write_text('run_to = "Monster"\n', encoding="utf-8")
        new_file.write_text("auto_skip = true\n", encoding="utf-8")
        (self.game / "sacore" / "config" / "core.toml").write_text("[mods]\nenabled = false\n", encoding="utf-8")

        self.assertTrue(runner.restore_journal(self.testing))
        self.assertEqual((self.mods / "mission_pass.toml").read_text(encoding="utf-8"), 'run_to = "off"\n')
        self.assertFalse(new_file.exists())
        self.assertEqual((self.game / "sacore" / "config" / "core.toml").read_text(encoding="utf-8"),
                         "[mods]\nenabled = true\n")
        self.assertFalse((self.testing / runner.CONFIG_JOURNAL).exists())
        self.assertFalse(runner.restore_journal(self.testing))  # nothing left to undo


if __name__ == "__main__":
    unittest.main()
