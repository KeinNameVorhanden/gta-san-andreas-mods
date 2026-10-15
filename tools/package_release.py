"""Build the SACore install package dist/SACore-<version>/ and dist/SACore-<version>.zip.

Contents:
    install.py            the installer (tools/release/installer.py)
    manifest.json         version, files with sha256 and component group, supported builds
    payload/              files exactly as they land in the game folder
        dinput8.dll                   Ultimate ASI Loader (downloads/ual)
        scripts/SilentPatchSA.asi/.ini SilentPatch (downloads/silentpatch)
        scripts/SACore.asi            the framework (Release build)
        sacore/mods/<id>/             optional: every mod in mods/ (install.py --with <ids> or --with all)
    INSTALL.md, RELEASE_NOTES.md, LICENSE, THIRD_PARTY.md, licenses/

The package bundles two third-party MIT components (Ultimate ASI Loader, SilentPatch) with their license texts;
tools/fetch_components.py downloads their release archives into downloads/ and checks their SHA-256.

Usage:
    python tools/package_release.py [--config Release]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tomllib
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import symbols  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "build" / "x86"
DOWNLOADS = REPO / "downloads"
DIST = REPO / "dist"
# Every mod with a manifest is an optional component (installed with --with <id>,... or --with all); mods/testing
# holds the test mods and is never shipped.
OPTIONAL_MODS = tuple(sorted(p.parent.name for p in (REPO / "mods").glob("*/mod.toml")))
ARCHIVES = {
    "ual": (DOWNLOADS / "ual" / "Ultimate-ASI-Loader-v9.7.4.zip", {"dinput8.dll": "dinput8.dll"}),
    "silentpatch": (DOWNLOADS / "silentpatch" / "SilentPatchSA-34.1.zip",
                    {"SilentPatchSA.asi": "scripts/SilentPatchSA.asi",
                     "SilentPatchSA.ini": "scripts/SilentPatchSA.ini",
                     "ReadMe.txt": "../licenses/SilentPatchSA-ReadMe.txt"}),
}
LICENSES = {
    "MinHook-LICENSE.txt": BUILD / "_deps" / "minhook-src" / "LICENSE.txt",
    "tomlplusplus-LICENSE.txt": BUILD / "_deps" / "tomlplusplus-src" / "LICENSE",
    "imgui-LICENSE.txt": BUILD / "_deps" / "imgui-src" / "LICENSE.txt",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args(argv)

    version = (REPO / "VERSION").read_text(encoding="utf-8").strip()
    root = DIST / f"SACore-{version}"
    if root.exists():
        target = root.resolve()
        if DIST.resolve() not in target.parents:
            print(f"REFUSED to replace {target}: not inside {DIST.resolve()}")
            return 2
        shutil.rmtree(target)
    payload = root / "payload"
    files: list[dict] = []

    def add(source: Path, rel: str, group: str) -> None:
        if source.suffix.lower() in (".asi", ".dll"):
            symbols.archive(source)  # crash triage needs the map of exactly the shipped binary
        target = payload / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files.append({"path": rel, "sha256": sha256(target), "group": group})

    missing = []
    for name, (archive, mapping) in ARCHIVES.items():
        if not archive.is_file():
            missing.append(str(archive))
            continue
        with zipfile.ZipFile(archive) as zf:
            for member, rel in mapping.items():
                data = zf.read(member)
                if rel.startswith("../"):
                    target = root / rel[3:]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    continue
                target = payload / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                files.append({"path": rel, "sha256": sha256(target), "group": "base"})
    core = BUILD / "src" / "core" / args.config / "SACore.asi"
    if not core.is_file():
        missing.append(str(core))
    else:
        add(core, "scripts/SACore.asi", "base")
    for name in OPTIONAL_MODS:
        manifest = REPO / "mods" / name / "mod.toml"
        data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        dll = BUILD / "mods" / name / args.config / data["dll"]
        if not dll.is_file():
            missing.append(str(dll))
            continue
        add(manifest, f"sacore/mods/{data['id']}/mod.toml", name)
        add(dll, f"sacore/mods/{data['id']}/{data['dll']}", name)
    for name, source in LICENSES.items():
        if not source.is_file():
            missing.append(str(source))
            continue
        (root / "licenses").mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, root / "licenses" / name)
    if missing:
        for path in missing:
            print(f"MISSING {path}")
        return 1

    known = json.loads((REPO / "tools" / "data" / "known_builds.json").read_text(encoding="utf-8"))["builds"]
    manifest = {
        "version": version,
        "optional_groups": list(OPTIONAL_MODS),
        "known_builds": [{"id": b["id"], "exe": b["exe"], "sha256": b["sha256"]} for b in known],
        "components": {"Ultimate ASI Loader": "9.7.4", "SilentPatch SA": "34.1", "SACore": version,
                       "MinHook": "1.3.4", "toml++": "3.4.0", "Dear ImGui": "1.92.9b"},
        "files": files,
    }
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(REPO / "tools" / "release" / "installer.py", root / "install.py")
    for name, source in (("INSTALL.md", REPO / "docs" / "INSTALL.md"),
                         ("RELEASE_NOTES.md", REPO / "docs" / "RELEASE_NOTES.md"),
                         ("LICENSE", REPO / "LICENSE"), ("THIRD_PARTY.md", REPO / "THIRD_PARTY.md")):
        if source.is_file():
            shutil.copy2(source, root / name)
        else:
            print(f"note: {source.relative_to(REPO)} not found, not included")

    archive = DIST / f"SACore-{version}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(p for p in root.rglob("*") if p.is_file()):
            zf.write(path, path.relative_to(DIST).as_posix())
    print(f"package: {root}")
    print(f"archive: {archive} ({archive.stat().st_size // 1024} KiB, {len(files)} payload files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
