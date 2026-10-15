"""Package a built mod into dist/<id>-<version>.zip, the format tools/modman.py install accepts.

The archive contains <id>/mod.toml, <id>/<dll> (if the mod has code), <id>/content/... (if present) and
<id>/README.md (if present). The manifest is validated first.

Usage:
    python tools/package_mod.py <mod folder name> [--config Release]
"""
from __future__ import annotations

import argparse
import re
import sys
import tomllib
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name")
    parser.add_argument("--config", default="Release")
    args = parser.parse_args(argv)

    source = REPO / "mods" / args.name
    manifest_file = source / "mod.toml"
    if not manifest_file.is_file():
        print(f"no mods/{args.name}/mod.toml")
        return 2
    manifest = tomllib.loads(manifest_file.read_text(encoding="utf-8"))
    problems = []
    mod_id = manifest.get("id", "")
    if not re.fullmatch(r"[a-z0-9_.-]{1,64}", mod_id):
        problems.append(f"invalid id '{mod_id}'")
    if not re.fullmatch(r"\d+(\.\d+){0,2}", str(manifest.get("version", ""))):
        problems.append(f"invalid version '{manifest.get('version')}'")
    if manifest.get("api_version") != 1:
        problems.append("api_version must be 1")
    entries: list[tuple[Path, str]] = [(manifest_file, f"{mod_id}/mod.toml")]
    if manifest.get("dll"):
        dll = REPO / "build" / "x86" / "mods" / args.name / args.config / manifest["dll"]
        if not dll.is_file():
            problems.append(f"{dll} not built (cmake --build --preset x86-{args.config.lower()} --target {args.name})")
        entries.append((dll, f"{mod_id}/{manifest['dll']}"))
    for file in sorted(p for p in (source / "content").rglob("*") if p.is_file()):
        entries.append((file, f"{mod_id}/content/{file.relative_to(source / 'content').as_posix()}"))
    if (source / "README.md").is_file():
        entries.append((source / "README.md", f"{mod_id}/README.md"))
    if problems:
        for problem in problems:
            print(f"PROBLEM {problem}")
        return 1
    target = REPO / "dist" / f"{mod_id}-{manifest['version']}.zip"
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, name in entries:
            archive.write(path, name)
            print(f"add {name}")
    print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
