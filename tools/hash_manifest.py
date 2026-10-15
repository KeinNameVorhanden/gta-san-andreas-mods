"""Create or verify a SHA256 manifest of a directory tree (read-only on the tree).

Used to prove the original Steam install stays untouched and to pin test copies.

Usage:
    python tools/hash_manifest.py create <dir> <manifest.json>
    python tools/hash_manifest.py verify <dir> <manifest.json>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common.files import manifest_diff as diff, tree_manifest as build  # noqa: E402


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[0] not in ("create", "verify"):
        print(__doc__)
        return 2
    mode, root, manifest = argv[0], Path(argv[1]), Path(argv[2])
    if not root.is_dir():
        print(f"ERROR: {root} is not a directory")
        return 2
    if mode == "create":
        if manifest.exists():
            print(f"ERROR: {manifest} already exists, refusing to overwrite")
            return 2
        data = build(root)
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({"root": str(root), "files": data}, indent=1), encoding="utf-8")
        print(f"wrote {len(data)} entries to {manifest}")
        return 0
    expected = json.loads(manifest.read_text(encoding="utf-8"))["files"]
    changes = diff(expected, build(root))
    for line in changes:
        print(line)
    print("OK: tree matches manifest" if not changes else f"{len(changes)} difference(s)")
    return 0 if not changes else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
