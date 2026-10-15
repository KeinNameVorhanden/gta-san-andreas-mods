"""Set or clear the Large Address Aware flag (4 GB address space instead of 2 GB) in a game copy's executable.

Only for copies of the game, never the Steam folder; the exe before the change is
kept in <game>/sacore/install/backups. SACore and the tools identify the exe by its hash with this flag cleared, so a
patched copy is still its build. Dry-run unless --apply.

Usage:
    python tools/laa.py --game <copy> [--remove] [--apply]
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import shutil
import struct
import sys
from pathlib import Path

KNOWN_BUILDS = Path(__file__).resolve().parent / "data" / "known_builds.json"
EXES = ("gta-sa.exe", "gta_sa.exe")
LARGE_ADDRESS_AWARE = 0x20


def flag_offset(data: bytes) -> int:
    """Offset of IMAGE_FILE_HEADER.Characteristics (low byte holds the flag)."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE executable")
    return pe + 4 + 18


def identity(data: bytes) -> tuple[str, bool]:
    """(SHA-256 with the flag cleared, flag set)."""
    at = flag_offset(data)
    laa = bool(data[at] & LARGE_ADDRESS_AWARE)
    plain = bytearray(data)
    plain[at] &= ~LARGE_ADDRESS_AWARE & 0xFF
    return hashlib.sha256(plain).hexdigest(), laa


def main(argv: list[str], known_builds: Path = KNOWN_BUILDS) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--remove", action="store_true", help="clear the flag again (2 GB)")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args(argv)

    game = args.game.resolve()
    if "steamapps" in str(game).lower():
        print(f"REFUSED: {game} looks like a Steam install; patch a copy")
        return 2
    candidates = [game / name for name in EXES if (game / name).is_file()]
    if not candidates:
        print(f"REFUSED: no game executable in {game}")
        return 2
    builds = json.loads(known_builds.read_text(encoding="utf-8"))["builds"]
    # A 1.0 US copy can hold the Steam file name as well (left by the downgrade): patch the exe the build starts.
    found = []
    for path in candidates:
        path_data = path.read_bytes()
        path_digest, path_laa = identity(path_data)
        known = next((b for b in builds if b["sha256"] == path_digest), None)
        if known is not None:
            other_name = known.get("exe", path.name).lower() != path.name.lower()
            found.append((other_name, path, path_data, path_digest, path_laa, known["id"]))
    if not found:
        digest = identity(candidates[0].read_bytes())[0]
        print(f"REFUSED: {candidates[0].name} is not a known build (sha256 without the flag {digest})")
        return 2
    _, exe, data, digest, laa, build = min(found, key=lambda item: item[0])
    want = not args.remove
    print(f"{exe}: build {build}, Large Address Aware {'set' if laa else 'not set'}")
    if laa == want:
        print("nothing to do")
        return 0
    try:
        with exe.open("r+b"):
            pass
    except OSError:
        print("REFUSED: the executable is in use (close the game first)")
        return 2
    backups = game / "sacore" / "install" / "backups"
    backup = backups / f"{exe.name}.{datetime.datetime.now():%Y%m%d-%H%M%S}.before-{'laa' if want else 'no-laa'}"
    print(f"{'set' if want else 'clear'} the flag; the current file is kept as {backup}")
    if not args.apply:
        print("Dry-run only. Re-run with --apply.")
        return 0
    backups.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, backup)
    patched = bytearray(data)
    at = flag_offset(data)
    patched[at] = (patched[at] | LARGE_ADDRESS_AWARE) if want else (patched[at] & ~LARGE_ADDRESS_AWARE & 0xFF)
    exe.write_bytes(bytes(patched))
    if identity(exe.read_bytes()) != (digest, want):
        shutil.copy2(backup, exe)
        print("FAILED: the written file does not check out; the backup was put back")
        return 1
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
