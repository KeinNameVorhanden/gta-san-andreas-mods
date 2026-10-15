"""Game-agnostic modding workshop utilities (the game folder is only read, never written).

Usage:
    python tools/common/workshop.py detect <game folder> [--json]          # engine, graphics API, loaders, PE facts
    python tools/common/workshop.py manifest create <folder> <out.json>     # SHA-256 manifest of a folder tree
    python tools/common/workshop.py manifest verify <folder> <manifest.json>
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import detect  # noqa: E402
import files  # noqa: E402


def cmd_detect(folder: Path, as_json: bool) -> int:
    report = detect.detect(folder)
    if as_json:
        print(json.dumps(asdict(report), indent=2))
        return 0 if not report.problems else 1
    print(f"folder:   {report.folder}")
    print(f"exe:      {report.exe or '-'}")
    if report.pe:
        print("pe:       " + ", ".join(f"{k} {v}" for k, v in report.pe.items() if v not in (None, False)))
    for title, findings in (("engine", report.engine), ("graphics", report.graphics), ("loaders", report.loaders)):
        if not findings:
            print(f"{title + ':':9} none found")
        for index, finding in enumerate(findings):
            print(f"{(title + ':') if index == 0 else '':9} {finding.what} ({finding.evidence})")
    for problem in report.problems:
        print(f"PROBLEM   {problem}")
    return 0 if not report.problems else 1


def cmd_manifest(action: str, folder: Path, manifest: Path) -> int:
    if action == "create":
        if manifest.exists():
            print(f"REFUSED: {manifest} exists")
            return 2
        manifest.write_text(json.dumps({"root": str(folder), "files": files.tree_manifest(folder)}, indent=1),
                            encoding="utf-8")
        print(f"wrote {manifest}")
        return 0
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    lines = files.manifest_diff(expected.get("files", expected), files.tree_manifest(folder))
    print("\n".join(lines) if lines else "OK: tree matches the manifest")
    return 1 if lines else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    detect_parser = commands.add_parser("detect")
    detect_parser.add_argument("folder", type=Path)
    detect_parser.add_argument("--json", action="store_true")
    manifest_parser = commands.add_parser("manifest")
    manifest_parser.add_argument("action", choices=["create", "verify"])
    manifest_parser.add_argument("folder", type=Path)
    manifest_parser.add_argument("manifest", type=Path)
    args = parser.parse_args(argv)
    if args.command == "detect":
        return cmd_detect(args.folder, args.json)
    return cmd_manifest(args.action, args.folder, args.manifest)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
