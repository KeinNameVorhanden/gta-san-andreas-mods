"""Bundle SACore diagnostics of one game install into a zip for crash triage.

Included: sacore/logs/*, sacore/crashes/* (reports and minidumps), sacore/diagnostics/*, sacore/config/*.toml,
a listing (name, size, sha256) of the install's scripts/ and sacore/mods/ folders, and the inventory report.
Never included: save games, gta_sa.set, anything from the user profile, game data files.

Usage:
    python tools/collect_logs.py --game <install> --out <directory> [--dry-run]
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import io
import subprocess
import sys
import zipfile
from pathlib import Path

INCLUDE_DIRS = ("sacore/logs", "sacore/crashes", "sacore/diagnostics")
LISTED_DIRS = ("scripts", "sacore/mods")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    game = args.game.resolve()
    if not (game / "sacore").is_dir():
        print(f"no sacore folder in {game}: nothing to collect")
        return 1
    files: list[Path] = []
    for rel in INCLUDE_DIRS:
        files += sorted(p for p in (game / rel).rglob("*") if p.is_file())
    files += sorted((game / "sacore" / "config").glob("*.toml"))

    listing = io.StringIO()
    for rel in LISTED_DIRS:
        for path in sorted(p for p in (game / rel).rglob("*") if p.is_file()):
            listing.write(f"{path.relative_to(game).as_posix()}\t{path.stat().st_size}\t{sha256(path)}\n")
    inventory = subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("inventory.py")), "--path", str(game)],
                               capture_output=True, text=True).stdout

    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    target = args.out.resolve() / f"sacore-diagnostics-{game.name}-{stamp}.zip"
    for path in files:
        print(f"include {path.relative_to(game).as_posix()} ({path.stat().st_size} bytes)")
    print("include file-listing.txt, inventory.txt")
    if args.dry_run:
        print(f"Dry-run only. Would write {target}")
        return 0
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, path.relative_to(game).as_posix())
        archive.writestr("file-listing.txt", listing.getvalue())
        archive.writestr("inventory.txt", inventory)
    print(f"wrote {target} ({target.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
