"""Download the third-party components the install package bundles into downloads/, checked by SHA-256.

    Ultimate ASI Loader 9.7.4  (MIT)  github.com/ThirteenAG/Ultimate-ASI-Loader
    SilentPatch SA 34.1        (MIT)  github.com/CookiePLMonster/SilentPatch

Files that are already there with the right hash are kept. A download with another hash is refused and deleted,
nothing else is written. tools/package_release.py reads the archives from these paths.

Usage:
    python tools/fetch_components.py
"""
from __future__ import annotations

import hashlib
import sys
import urllib.request
from pathlib import Path

DOWNLOADS = Path(__file__).resolve().parent.parent / "downloads"
COMPONENTS = (
    ("Ultimate ASI Loader 9.7.4",
     "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.4/Ultimate-ASI-Loader.zip",
     DOWNLOADS / "ual" / "Ultimate-ASI-Loader-v9.7.4.zip",
     "952cebfc30d525afc2bdbaca954329d405ded3aa688a83027354dae14dfd5c5f"),
    ("SilentPatch SA 34.1",
     "https://github.com/CookiePLMonster/SilentPatch/releases/download/1.1-BUILD34.1-SA/SilentPatchSA.zip",
     DOWNLOADS / "silentpatch" / "SilentPatchSA-34.1.zip",
     "0837bbdc0548a4a952f924adefd2bb6eacc866407f153844fe1f56995668c42c"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    failed = False
    for name, url, target, expected in COMPONENTS:
        if target.is_file() and sha256(target) == expected:
            print(f"ok       {name}: {target.relative_to(DOWNLOADS.parent)}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_suffix(".part")
        with urllib.request.urlopen(url, timeout=60) as response:  # noqa: S310 (fixed https URLs)
            partial.write_bytes(response.read())
        actual = sha256(partial)
        if actual != expected:
            partial.unlink()
            print(f"REFUSED  {name}: SHA-256 {actual}, expected {expected}")
            failed = True
            continue
        partial.replace(target)
        print(f"fetched  {name}: {target.relative_to(DOWNLOADS.parent)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
