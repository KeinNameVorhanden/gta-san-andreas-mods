"""Check whether a mod (folder or zip) fits a game install: game build, SACore version, mod API, dependencies.

Usage:
    python tools/compat_check.py --game <install> <mods/<name> folder | mod zip>
"""
from __future__ import annotations

import argparse
import sys
import tomllib
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inventory  # noqa: E402
from common.files import file_version  # noqa: E402
import modman  # noqa: E402


def load_manifest(source: Path) -> dict:
    if source.is_dir():
        return tomllib.loads((source / "mod.toml").read_text(encoding="utf-8"))
    with zipfile.ZipFile(source) as archive:
        name = next(n for n in archive.namelist() if n.endswith("mod.toml") and n.count("/") <= 1)
        return tomllib.loads(archive.read(name).decode("utf-8"))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("mod", type=Path)
    args = parser.parse_args(argv)

    manifest = load_manifest(args.mod)
    game = args.game.resolve()
    problems: list[str] = []
    notes: list[str] = []

    report = inventory.inspect_install(game, "manual", inventory.load_known_builds())
    builds = {e.build for e in report.executables if e.build}
    if not builds:
        problems.append("no supported game executable in the install")
    targets = manifest.get("target_builds", [])
    if targets and not builds.intersection(targets):
        problems.append(f"mod targets {targets}, install has {sorted(builds)}")

    core = game / "scripts" / "SACore.asi"
    core_version = file_version(core) if core.is_file() else None
    if core_version is None:
        problems.append("SACore.asi not installed (or without version information)")
    else:
        notes.append(f"SACore {core_version}")
        minimum = manifest.get("min_core_version")
        parsed = modman.parse_version(core_version)
        if minimum and parsed and not modman.satisfies(parsed, f">={minimum}"):
            problems.append(f"mod needs SACore {minimum} or newer, install has {core_version}")
    if manifest.get("api_version") != modman.CORE_API_VERSION:
        problems.append(f"mod built for API {manifest.get('api_version')}, core provides {modman.CORE_API_VERSION}")

    installed = {}
    for item in modman.Install(game).items():
        if item.key.startswith("mod:") and item.enabled:
            data = modman.Install._manifest(item.path / "mod.toml") or {}
            installed[data.get("id", item.path.name)] = data.get("version", "")
    for dep, requirement in manifest.get("dependencies", {}).items():
        version = modman.parse_version(str(installed.get(dep, "")))
        if dep not in installed:
            problems.append(f"requires '{dep}' {requirement}, not installed or disabled")
        elif version is None or not modman.satisfies(version, requirement):
            problems.append(f"requires '{dep}' {requirement}, installed {installed[dep]}")

    print(f"mod {manifest.get('id')} {manifest.get('version')} vs {game} ({', '.join(sorted(builds)) or 'unknown build'})")
    for note in notes:
        print(f"  ok      {note}")
    for problem in problems:
        print(f"  PROBLEM {problem}")
    print("compatible" if not problems else f"{len(problems)} problem(s)")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
