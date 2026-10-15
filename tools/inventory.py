"""Read-only GTA San Andreas install inventory.

Finds installs (Steam libraries, registry uninstall entries, Rockstar Games
Launcher, manual paths), hashes the game executable and maps it to a known
build from tools/data/known_builds.json. Never writes to any install.

Usage:
    python tools/inventory.py                 # auto-detect
    python tools/inventory.py --path <dir>    # inspect one directory
    python tools/inventory.py --json          # machine-readable output
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common.files import sha256_file as sha256_of  # noqa: E402
import laa  # noqa: E402

EXE_NAMES = ("gta_sa.exe", "gta-sa.exe", "gta_sa_compact.exe")
KNOWN_BUILDS_FILE = Path(__file__).resolve().parent / "data" / "known_builds.json"
STEAM_APP_ID = "12120"


@dataclass
class ExeInfo:
    name: str
    size: int
    sha256: str
    build: str | None
    build_note: str | None


@dataclass
class InstallReport:
    path: str
    source: str
    exists: bool
    executables: list[ExeInfo] = field(default_factory=list)
    asi_files: list[str] = field(default_factory=list)
    has_cleo_dir: bool = False
    has_modloader_dir: bool = False
    has_scripts_dir: bool = False
    problems: list[str] = field(default_factory=list)


def load_known_builds() -> dict[str, dict]:
    data = json.loads(KNOWN_BUILDS_FILE.read_text(encoding="utf-8"))
    return {entry["sha256"].lower(): entry for entry in data["builds"]}


def inspect_install(path: Path, source: str, known: dict[str, dict]) -> InstallReport:
    report = InstallReport(path=str(path), source=source, exists=path.is_dir())
    if not report.exists:
        report.problems.append("directory does not exist")
        return report
    for name in EXE_NAMES:
        exe = path / name
        if exe.is_file():
            digest = laa.identity(exe.read_bytes())[0]  # the 4 GB flag (tools/laa.py) does not change the build
            entry = known.get(digest)
            report.executables.append(ExeInfo(
                name=name,
                size=exe.stat().st_size,
                sha256=digest,
                build=entry["id"] if entry else None,
                build_note=entry.get("note") if entry else None,
            ))
    if not report.executables:
        report.problems.append("no GTA SA executable found (" + ", ".join(EXE_NAMES) + ")")
    elif all(e.build is None for e in report.executables):
        report.problems.append("executable hash not in known_builds.json: unsupported or unknown build")
    report.asi_files = sorted(str(p.relative_to(path)) for p in path.rglob("*.asi"))
    report.has_cleo_dir = (path / "cleo").is_dir()
    report.has_modloader_dir = (path / "modloader").is_dir()
    report.has_scripts_dir = (path / "scripts").is_dir()
    return report


def steam_library_roots() -> list[Path]:
    """Steam library folders from libraryfolders.vdf, read via the registry Steam path."""
    roots: list[Path] = []
    steam_path = registry_value(r"Software\Valve\Steam", "SteamPath", hive="HKCU")
    if steam_path:
        vdf = Path(steam_path) / "steamapps" / "libraryfolders.vdf"
        if vdf.is_file():
            text = vdf.read_text(encoding="utf-8", errors="replace")
            for match in re.finditer(r'"path"\s+"([^"]+)"', text):
                roots.append(Path(match.group(1).replace("\\\\", "\\")))
    return roots


def registry_value(key: str, value: str, hive: str = "HKLM") -> str | None:
    try:
        import winreg
    except ImportError:
        return None
    root = winreg.HKEY_CURRENT_USER if hive == "HKCU" else winreg.HKEY_LOCAL_MACHINE
    for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
        try:
            with winreg.OpenKey(root, key, 0, winreg.KEY_READ | view) as handle:
                result, _ = winreg.QueryValueEx(handle, value)
                return str(result)
        except OSError:
            continue
    return None


def registry_uninstall_locations() -> list[Path]:
    try:
        import winreg
    except ImportError:
        return []
    found: list[Path] = []
    base = r"Software\Microsoft\Windows\CurrentVersion\Uninstall"
    for root in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(root, base, 0, winreg.KEY_READ | view) as handle:
                    for index in range(winreg.QueryInfoKey(handle)[0]):
                        name = winreg.EnumKey(handle, index)
                        try:
                            with winreg.OpenKey(handle, name) as sub:
                                display = winreg.QueryValueEx(sub, "DisplayName")[0]
                                location = winreg.QueryValueEx(sub, "InstallLocation")[0]
                        except OSError:
                            continue
                        if "San Andreas" in str(display) and location:
                            found.append(Path(location))
            except OSError:
                continue
    return found


def discover() -> list[tuple[Path, str]]:
    candidates: list[tuple[Path, str]] = []
    for root in steam_library_roots():
        manifest = root / "steamapps" / f"appmanifest_{STEAM_APP_ID}.acf"
        if manifest.is_file():
            text = manifest.read_text(encoding="utf-8", errors="replace")
            match = re.search(r'"installdir"\s+"([^"]+)"', text)
            if match:
                candidates.append((root / "steamapps" / "common" / match.group(1), "steam"))
    for location in registry_uninstall_locations():
        candidates.append((location, "registry"))
    rgl = registry_value(r"Software\Rockstar Games\GTA San Andreas", "InstallFolder")
    if rgl:
        candidates.append((Path(rgl), "rockstar-launcher"))
    unique: dict[str, tuple[Path, str]] = {}
    for path, source in candidates:
        unique.setdefault(str(path.resolve()).lower(), (path, source))
    return list(unique.values())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--path", action="append", default=[], help="inspect this directory (repeatable)")
    parser.add_argument("--json", action="store_true", help="print JSON instead of text")
    args = parser.parse_args(argv)

    known = load_known_builds()
    targets = [(Path(p), "manual") for p in args.path] if args.path else discover()
    reports = [inspect_install(path, source, known) for path, source in targets]

    if args.json:
        print(json.dumps([asdict(r) for r in reports], indent=2))
    else:
        if not reports:
            print("No GTA San Andreas install found. Use --path <dir>.")
        for r in reports:
            print(f"[{r.source}] {r.path}")
            for e in r.executables:
                print(f"  {e.name}  {e.size} bytes  sha256 {e.sha256}")
                print(f"    build: {e.build or 'UNKNOWN'}" + (f" ({e.build_note})" if e.build_note else ""))
            print(f"  asi files: {', '.join(r.asi_files) or 'none'}")
            print(f"  cleo/: {r.has_cleo_dir}  modloader/: {r.has_modloader_dir}  scripts/: {r.has_scripts_dir}")
            for p in r.problems:
                print(f"  PROBLEM: {p}")
    ok = bool(reports) and all(not r.problems for r in reports)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
