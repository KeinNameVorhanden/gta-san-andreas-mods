"""Create a new SACore mod project from templates/mod.

Creates mods/<id>/ with mod.toml, CMakeLists.txt, src/<id>.cpp and README.md. The mods/ CMake folder picks it up
automatically; reconfigure (cmake --preset x86) and build it with --target <id>.

Usage:
    python tools/new_mod.py <id> --name "My Mod" [--author NAME] [--description TEXT]
                            [--builds newsteam_r2_lv,us10_hoodlum] [--dry-run]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEMPLATE = REPO / "templates" / "mod"
KNOWN_BUILDS = {e["id"] for e in json.loads((REPO / "tools" / "data" / "known_builds.json").read_text(encoding="utf-8"))["builds"]}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("id")
    parser.add_argument("--name", required=True)
    parser.add_argument("--author", default="")
    parser.add_argument("--description", default="")
    parser.add_argument("--builds", default="", help="comma-separated build ids; empty = every supported build")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    if not re.fullmatch(r"[a-z0-9_]{1,64}", args.id):
        print("mod id must be 1 to 64 of [a-z0-9_] (it is also the DLL and CMake target name)")
        return 2
    builds = [b.strip() for b in args.builds.split(",") if b.strip()]
    unknown = [b for b in builds if b not in KNOWN_BUILDS]
    if unknown:
        print(f"unknown build id(s) {unknown}; known: {sorted(KNOWN_BUILDS)}")
        return 2
    target = REPO / "mods" / args.id
    if target.exists():
        print(f"REFUSED: {target} already exists")
        return 2

    core_version = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    replacements = {
        "{{ID}}": args.id,
        "{{NAME}}": args.name.replace('"', "'"),
        "{{AUTHOR}}": args.author.replace('"', "'"),
        "{{DESCRIPTION}}": args.description.replace('"', "'"),
        "{{CORE_VERSION}}": core_version,
        "{{TARGET_BUILDS}}": ("target_builds = [" + ", ".join(f'"{b}"' for b in builds) + "]") if builds else "",
    }
    files = []
    for source in sorted(p for p in TEMPLATE.rglob("*") if p.is_file()):
        rel = source.relative_to(TEMPLATE)
        if rel.as_posix() == "src/mod.cpp":
            rel = Path("src") / f"{args.id}.cpp"
        text = source.read_text(encoding="utf-8")
        for key, value in replacements.items():
            text = text.replace(key, value)
        files.append((target / rel, text))
    for path, _ in files:
        print(f"create {path.relative_to(REPO).as_posix()}")
    if args.dry_run:
        print("Dry-run only.")
        return 0
    for path, text in files:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print(f"next: cmake --preset x86 && cmake --build --preset x86-release --target {args.id}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
