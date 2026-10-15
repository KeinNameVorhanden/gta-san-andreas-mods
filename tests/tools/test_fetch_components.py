"""Behaviour tests for tools/fetch_components.py with local file:// sources (no network)."""
import hashlib
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import fetch_components  # noqa: E402


class FetchComponentsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.source = root / "upstream.zip"
        self.source.write_bytes(b"PK release archive")
        self.downloads = root / "downloads"
        self.saved = (fetch_components.DOWNLOADS, fetch_components.COMPONENTS)
        fetch_components.DOWNLOADS = self.downloads

    def tearDown(self):
        fetch_components.DOWNLOADS, fetch_components.COMPONENTS = self.saved
        self.temp.cleanup()

    def run_main(self, expected_hash: str) -> tuple[int, str, Path]:
        target = self.downloads / "ual" / "loader.zip"
        fetch_components.COMPONENTS = (("Loader", self.source.as_uri(), target, expected_hash),)
        out = io.StringIO()
        with redirect_stdout(out):
            code = fetch_components.main()
        return code, out.getvalue(), target

    def test_a_changed_upstream_archive_is_refused_and_leaves_nothing_behind(self):
        code, text, target = self.run_main("0" * 64)
        self.assertEqual(code, 1)
        self.assertIn("REFUSED", text)
        self.assertFalse(target.exists())
        self.assertEqual(list(target.parent.iterdir()), [])  # no .part file either

    def test_the_expected_archive_is_stored_and_kept_on_the_next_run(self):
        good = hashlib.sha256(self.source.read_bytes()).hexdigest()
        code, text, target = self.run_main(good)
        self.assertEqual(code, 0, text)
        self.assertEqual(target.read_bytes(), b"PK release archive")
        self.source.write_bytes(b"would be a different download")
        code, text, _ = self.run_main(good)
        self.assertEqual(code, 0)
        self.assertIn("ok", text)
        self.assertEqual(target.read_bytes(), b"PK release archive")


if __name__ == "__main__":
    unittest.main()
