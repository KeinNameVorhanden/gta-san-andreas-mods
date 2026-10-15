"""Archive the linker map and PDB of a built SACore binary under symbols/<sha8>/, keyed by the binary's SHA-256.

Crash reports name SACore.asi's SHA-256, so tools/triage_crash.py can resolve an old crash with exactly the map of
the binary that crashed, even after newer builds. The archive lives outside git (.gitignore: /symbols/).

Usage (called by tools/deploy_dev.py and tools/package_release.py; also usable by hand):
    python tools/symbols.py build/x86/src/core/Release/SACore.asi [more binaries ...]
"""
from __future__ import annotations

import hashlib
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SYMBOLS = REPO / "symbols"


def archive(binary: Path) -> Path | None:
    """Copies <binary>, .map and .pdb next to it into symbols/<sha8>/; returns the folder, None if no map exists."""
    map_file = binary.with_suffix(".map")
    if not binary.is_file() or not map_file.is_file():
        return None
    sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    target = SYMBOLS / sha[:8]
    target.mkdir(parents=True, exist_ok=True)
    for source in (binary, map_file, binary.with_suffix(".pdb")):
        if source.is_file() and not (target / source.name).is_file():
            shutil.copy2(source, target / source.name)
    (target / "sha256.txt").write_text(f"{sha}  {binary.name}\n", encoding="utf-8")
    return target


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    for name in argv:
        folder = archive(Path(name))
        print(f"{name}: {folder if folder else 'no map file next to it, not archived'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
