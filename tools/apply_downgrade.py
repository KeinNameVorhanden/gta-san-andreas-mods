"""Apply GTA SA Open Downgrader patches (manifest.json + xdelta files) to a test copy.

Re-implements the patch step of xxanqw/gtasa-open-downgrader v0.2.3 (MIT) without its GUI:
every input file is MD5-checked against the manifest's known sources, every output against
the manifest's target hash, and a file is replaced only after its output verified.

Dry-run by default. Never point it at the original Steam install: it refuses paths that
contain "steamapps".

Usage:
    python tools/apply_downgrade.py --game <dir> --patches <dir with manifest.json> --xdelta <xdelta3.exe> [--apply]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

EXE_NAMES = ("gta_sa.exe", "gta-sa.exe")


def md5_of(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_target(game: Path, rel_path: str) -> Path:
    target = game / rel_path
    if rel_path in EXE_NAMES and not target.exists():
        alt = game / (EXE_NAMES[1] if rel_path == EXE_NAMES[0] else EXE_NAMES[0])
        if alt.exists():
            return alt
    return target


def pick_patch(entry: dict, current_hash: str) -> str | None:
    """Return the patch file for the current source hash, or None if the source is unknown."""
    sources = entry.get("sources")
    if sources:
        match = next((s for s in sources if s.get("hash") == current_hash), None)
        return match.get("patch") if match else None
    if entry.get("source_hash") == current_hash:  # v1 manifest layout
        return f"{entry['path']}.xdelta"
    return None


def plan(game: Path, patches: Path, manifest: dict) -> tuple[list[dict], list[str]]:
    steps, problems = [], []
    for entry in manifest.get("files", []):
        rel = entry["path"]
        action = entry.get("action", "patch")
        target = resolve_target(game, rel)
        target_hash = entry.get("target_hash")
        if not target.exists() and action != "copy":
            problems.append(f"{rel}: source file missing")
            continue
        current = md5_of(target) if target.exists() else None
        if target_hash and current == target_hash:
            steps.append({"rel": rel, "target": target, "action": "skip", "why": "already at target hash"})
            continue
        if action == "copy":
            payload = patches / entry.get("payload", "gta_sa.exe")
            if not payload.exists():
                problems.append(f"{rel}: payload {payload.name} missing")
                continue
            steps.append({"rel": rel, "target": target, "action": "copy", "payload": payload, "target_hash": target_hash})
            continue
        patch_rel = pick_patch(entry, current)
        if patch_rel is None:
            problems.append(f"{rel}: unknown source MD5 {current}")
            continue
        patch_file = patches / patch_rel
        if not patch_file.exists():
            problems.append(f"{rel}: patch {patch_rel} missing")
            continue
        steps.append({"rel": rel, "target": target, "action": "patch", "patch": patch_file, "target_hash": target_hash})
    return steps, problems


def execute(step: dict, xdelta: Path) -> str:
    target: Path = step["target"]
    temp = target.with_name(target.name + ".downgrade.tmp")
    if step["action"] == "copy":
        temp.write_bytes(step["payload"].read_bytes())
    else:
        result = subprocess.run([str(xdelta), "-d", "-f", "-s", str(target), str(step["patch"]), str(temp)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            temp.unlink(missing_ok=True)
            return f"xdelta failed: {result.stderr.strip()[:200]}"
    if step.get("target_hash") and md5_of(temp) != step["target_hash"]:
        temp.unlink(missing_ok=True)
        return "output MD5 does not match target hash"
    os.replace(temp, target)
    return "ok"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--patches", required=True, type=Path)
    parser.add_argument("--xdelta", required=True, type=Path)
    parser.add_argument("--apply", action="store_true", help="actually change files (default: dry-run)")
    args = parser.parse_args(argv)

    game = args.game.resolve()
    if "steamapps" in str(game).lower():
        print("REFUSED: target looks like a Steam library install. Use a test copy.")
        return 2
    manifest = json.loads((args.patches / "manifest.json").read_text(encoding="utf-8"))
    steps, problems = plan(game, args.patches, manifest)

    print(f"Game: {game}")
    for step in steps:
        print(f"  {step['action']:5} {step['rel']}")
    for problem in problems:
        print(f"  PROBLEM {problem}")
    if problems:
        print("Stopping: fix the problems above first (no files changed).")
        return 1
    if not args.apply:
        print("Dry-run only. Re-run with --apply to change files.")
        return 0

    failures = 0
    for step in steps:
        if step["action"] == "skip":
            continue
        outcome = execute(step, args.xdelta)
        print(f"  {step['rel']}: {outcome}")
        failures += outcome != "ok"
    exe_names = [e for e in EXE_NAMES if (game / e).exists()]
    if len(exe_names) == 1:
        source = game / exe_names[0]
        other = game / (EXE_NAMES[1] if exe_names[0] == EXE_NAMES[0] else EXE_NAMES[0])
        other.write_bytes(source.read_bytes())
        print(f"  copied {source.name} -> {other.name} (both launch names present)")
    print("Done." if not failures else f"{failures} file(s) failed.")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
