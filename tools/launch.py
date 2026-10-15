"""Start a game install for development (no harness, no automatic closing).

By default the game starts without taking focus (use --activate to bring it to the front), so it never grabs the
mouse until you click into it. Refuses the Steam folder (use a test or play copy).

Usage:
    python tools/launch.py --game <install> [--activate]
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

EXE_NAMES = ("gta_sa.exe", "gta-sa.exe")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--activate", action="store_true")
    args = parser.parse_args(argv)
    game = args.game.resolve()
    if "steamapps" in str(game).lower():
        print("REFUSED: launch a test or play copy, not the Steam folder")
        return 2
    exe = next((game / n for n in EXE_NAMES if (game / n).is_file()), None)
    if exe is None:
        print(f"no game executable in {game}")
        return 2
    info = subprocess.STARTUPINFO()
    if not args.activate:
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = 4  # SW_SHOWNOACTIVATE
    process = subprocess.Popen([str(exe)], cwd=str(game), startupinfo=info)
    print(f"started {exe.name} (pid {process.pid}); log: {game / 'sacore' / 'logs' / 'sacore.log'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
