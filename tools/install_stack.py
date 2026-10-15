"""Install third-party base components into a GTA SA test copy (dry-run by default).

Components are read from release archives in downloads/ (kept out of git) and placed as:
    ual           -> dinput8.dll                      (Ultimate ASI Loader, proxy for DINPUT8.dll import)
    silentpatch   -> scripts/SilentPatchSA.asi, .ini
    windowedmode  -> scripts/III.VC.SA.WindowedMode.asi (1.0 US only)

Existing files are never overwritten unless --force is given; every overwritten file is
backed up next to the install under <install>/.stack-backup/. This script is superseded by
the mod manager.

Usage:
    python tools/install_stack.py --game <test copy> --components ual,silentpatch [--apply] [--force]
"""
from __future__ import annotations

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

DOWNLOADS = Path(__file__).resolve().parent.parent / "downloads"
COMPONENTS = {
    "ual": (DOWNLOADS / "ual" / "Ultimate-ASI-Loader-v9.7.4.zip", {"dinput8.dll": "dinput8.dll"}),
    "silentpatch": (DOWNLOADS / "silentpatch" / "SilentPatchSA-34.1.zip",
                    {"SilentPatchSA.asi": "scripts/SilentPatchSA.asi", "SilentPatchSA.ini": "scripts/SilentPatchSA.ini"}),
    "windowedmode": (DOWNLOADS / "windowedmode" / "III.VC.SA.WindowedMode-v2.2.zip",
                     {"III.VC.SA.WindowedMode.asi": "scripts/III.VC.SA.WindowedMode.asi"}),
}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--components", required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)

    game = args.game.resolve()
    if "steamapps" in str(game).lower():
        print("REFUSED: use a test copy, not the Steam install.")
        return 2
    if not any((game / exe).is_file() for exe in ("gta_sa.exe", "gta-sa.exe")):
        print(f"REFUSED: no GTA SA executable in {game}")
        return 2

    names = [n.strip() for n in args.components.split(",") if n.strip()]
    unknown = [n for n in names if n not in COMPONENTS]
    if unknown:
        print(f"unknown component(s): {', '.join(unknown)}; known: {', '.join(COMPONENTS)}")
        return 2

    actions = []
    for name in names:
        archive, mapping = COMPONENTS[name]
        if not archive.is_file():
            print(f"missing archive for {name}: {archive}")
            return 1
        for member, rel_target in mapping.items():
            target = game / rel_target
            state = "overwrite" if target.exists() else "new"
            if state == "overwrite" and not args.force:
                state = "skip (exists, use --force)"
            actions.append((name, archive, member, target, state))

    for name, _, member, target, state in actions:
        print(f"  [{name}] {member} -> {target.relative_to(game)}: {state}")
    if not args.apply:
        print("Dry-run only. Re-run with --apply.")
        return 0

    for name, archive, member, target, state in actions:
        if state.startswith("skip"):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if state == "overwrite":
            backup = game / ".stack-backup" / target.relative_to(game)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup)
        with zipfile.ZipFile(archive) as zf, zf.open(member) as src, target.open("wb") as dst:
            shutil.copyfileobj(src, dst)
        print(f"  installed {target.relative_to(game)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
