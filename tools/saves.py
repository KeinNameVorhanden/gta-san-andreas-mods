"""Inspect, validate and restore GTA SA save slots of one install (the game must not be running).

The User Files folder is <game>/sacore/userdata/GTA San Andreas User Files when [saves] separate_user_files is on,
otherwise Documents/GTA San Andreas User Files. Backups made by SACore live in <game>/sacore/save-backups/.

Usage:
    python tools/saves.py --game <dir> list
    python tools/saves.py --game <dir> backups <slot>
    python tools/saves.py --game <dir> restore <slot> <backup file name> [--apply]
    python tools/saves.py --game <dir> story-copy --from <other install> [--apply]

story-copy takes the story runner's story saves (GTASAst<N>.b, one per mission, made with mission_pass save_each)
from another install of the same game build, for example a test copy the runner has played through.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime
import shutil
import struct
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common.detect import main_executable  # noqa: E402
from common.files import sha256_file  # noqa: E402

SAVE_SIZE = 0x31800
CHECKSUM_OFFSET = 0x317FC


def check(data: bytes) -> str:
    """Same rules as src/core/saves/save_file.cpp."""
    if len(data) != SAVE_SIZE:
        return f"damaged: size {len(data)} bytes, expected {SAVE_SIZE}"
    if data[:5] != b"BLOCK":
        return "damaged: does not start with BLOCK"
    stored = struct.unpack_from("<I", data, CHECKSUM_OFFSET)[0]
    computed = sum(data[:CHECKSUM_OFFSET]) & 0xFFFFFFFF
    if stored != computed:
        return f"damaged: checksum {stored:#010x} stored, {computed:#010x} computed"
    return f"valid (version id {struct.unpack_from('<I', data, 5)[0]:#010x})"


def documents() -> Path:
    buffer = ctypes.create_unicode_buffer(260)
    ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buffer)  # CSIDL_PERSONAL
    return Path(buffer.value)


def user_files(game: Path) -> Path:
    config = game / "sacore" / "config" / "core.toml"
    separate = False
    if config.is_file():
        separate = bool(tomllib.loads(config.read_text(encoding="utf-8")).get("saves", {}).get("separate_user_files"))
    base = game / "sacore" / "userdata" if separate else documents()
    return base / "GTA San Andreas User Files"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--apply", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")
    sub.add_parser("backups").add_argument("slot", type=int)
    restore = sub.add_parser("restore")
    restore.add_argument("slot", type=int)
    restore.add_argument("backup")
    story = sub.add_parser("story-copy")
    story.add_argument("--from", dest="source", required=True, type=Path)
    args = parser.parse_args(argv)

    game = args.game.resolve()
    folder = user_files(game)
    backups = game / "sacore" / "save-backups"
    print(f"User Files folder: {folder}")
    if args.command == "list":
        for slot in range(1, 9):
            file = folder / f"GTASAsf{slot}.b"
            if file.is_file():
                count = len(list((backups / file.name).glob("*.b")))
                print(f"slot {slot}: {check(file.read_bytes())}, {count} backup(s)")
        return 0
    if args.command == "story-copy":
        return story_copy(game, args.source.resolve(), folder, backups, args.apply)
    if not 1 <= args.slot <= 8:
        raise SystemExit("slot must be 1..8")
    slot_name = f"GTASAsf{args.slot}.b"
    if args.command == "backups":
        for backup in sorted((backups / slot_name).glob("*.b")):
            print(f"{backup.name}: {check(backup.read_bytes())}")
        return 0
    source = backups / slot_name / args.backup
    if not source.is_file() or source.parent != backups / slot_name:
        raise SystemExit(f"no backup {args.backup} for slot {args.slot} (see 'backups {args.slot}')")
    verdict = check(source.read_bytes())
    if not verdict.startswith("valid"):
        raise SystemExit(f"refusing to restore a {verdict} backup")
    target = folder / slot_name
    print(f"restore {source} -> {target} ({verdict})")
    if target.is_file():
        print(f"the current {slot_name} is kept as {slot_name}.before-restore-<timestamp>.b in the backups folder")
    if not args.apply:
        print("Dry-run only. Re-run with --apply.")
        return 0
    if target.is_file():
        keep = backups / slot_name / f"{datetime.datetime.now():%Y-%m-%d_%H-%M-%S}.before-restore.b"
        shutil.copy2(target, keep)
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    print("restored")
    return 0


def story_copy(game: Path, source: Path, folder: Path, backups: Path, apply: bool) -> int:
    """Copies the valid story saves of `source` into this install; a save only loads in the build that wrote it."""
    exes = [main_executable(game), main_executable(source)]
    if None in exes or sha256_file(exes[0]) != sha256_file(exes[1]):
        raise SystemExit("refusing: the two installs are not the same game build (story saves only load in theirs)")
    files = sorted(user_files(source).glob("GTASAst*.b"), key=lambda f: int(f.stem[7:] or 0))
    if not files:
        raise SystemExit(f"no story saves in {user_files(source)}")
    copied = 0
    for file in files:
        verdict = check(file.read_bytes())
        if not verdict.startswith("valid"):
            print(f"skip {file.name}: {verdict}")
            continue
        target = folder / file.name
        print(f"copy {file.name} ({verdict}){' over the existing one, kept as a backup' if target.is_file() else ''}")
        copied += 1
        if apply:
            if target.is_file():
                keep = backups / file.name
                keep.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, keep / f"{datetime.datetime.now():%Y-%m-%d_%H-%M-%S}.before-copy.b")
            folder.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
    print(f"{copied} story save(s) {'copied' if apply else 'to copy'}")
    if not apply:
        print("Dry-run only. Re-run with --apply.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
