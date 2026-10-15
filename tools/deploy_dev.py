"""Copy freshly built SACore artifacts into a test copy (dry-run by default).

Deploys:
    build/x86/src/core/<cfg>/SACore.asi            -> <game>/scripts/SACore.asi
    --test-mods: build/x86/mods/testing/<cfg>/<id>.dll + mods/testing/manifests/<id>.toml
                 -> <game>/sacore/mods/<id>/<id>.dll + mod.toml
    --remove-test-mods: deletes <game>/sacore/mods/test_* directories (absolute paths only)
    --mod <name>: mods/<name>/mod.toml + build/x86/mods/<name>/<cfg>/<dll from manifest> -> <game>/sacore/mods/<id>/

Usage:
    python tools/deploy_dev.py --game <test copy> [--config Release] [--test-mods | --remove-test-mods] [--mod devtools] [--apply]
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import symbols  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "build" / "x86"
MANIFESTS = REPO / "mods" / "testing" / "manifests"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--test-mods", action="store_true")
    parser.add_argument("--remove-test-mods", action="store_true")
    parser.add_argument("--mod", action="append", default=[], help="deploy mods/<name> (repeatable)")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args(argv)

    game = args.game.resolve()
    if "steamapps" in str(game).lower() or not any((game / e).is_file() for e in ("gta_sa.exe", "gta-sa.exe")):
        print(f"REFUSED: {game} is not a GTA SA test copy")
        return 2

    copies: list[tuple[Path, Path]] = [(BUILD / "src" / "core" / args.config / "SACore.asi", game / "scripts" / "SACore.asi")]
    removals: list[Path] = []
    if args.test_mods:
        content_mods = REPO / "mods" / "testing" / "content_mods"
        for source in sorted(p for p in content_mods.rglob("*") if p.is_file()):
            copies.append((source, game / "sacore" / "mods" / source.relative_to(content_mods)))
        for manifest in sorted(MANIFESTS.glob("*.toml")):
            mod_id = manifest.stem
            target_dir = game / "sacore" / "mods" / mod_id
            copies.append((manifest, target_dir / "mod.toml"))
            dll = BUILD / "mods" / "testing" / args.config / f"{mod_id}.dll"
            if dll.is_file():
                copies.append((dll, target_dir / f"{mod_id}.dll"))
    for name in args.mod:
        import tomllib
        manifest = REPO / "mods" / name / "mod.toml"
        data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        target_dir = game / "sacore" / "mods" / data["id"]
        copies.append((manifest, target_dir / "mod.toml"))
        copies.append((BUILD / "mods" / name / args.config / data["dll"], target_dir / data["dll"]))
    if args.remove_test_mods:
        removals = sorted(p.resolve() for p in (game / "sacore" / "mods").glob("test_*") if p.is_dir())

    missing = [src for src, _ in copies if not src.is_file()]
    for src in missing:
        print(f"MISSING build output: {src}")
    if missing:
        return 1
    for src, dst in copies:
        print(f"copy   {src.relative_to(REPO)} -> {dst} ({'overwrite' if dst.exists() else 'new'})")
    for path in removals:
        print(f"remove {path}")
    if not args.apply:
        print("Dry-run only. Re-run with --apply.")
        return 0
    for src, dst in copies:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        if src.suffix.lower() in (".asi", ".dll"):
            symbols.archive(src)  # map + PDB of exactly this binary, for crash triage
    for path in removals:
        if not path.is_absolute() or game not in path.parents:
            print(f"REFUSED to remove {path}: not inside {game}")
            return 2
        shutil.rmtree(path)
    print(f"done: {len(copies)} copied, {len(removals)} removed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
