"""SACore mod manager: list, enable/disable, profiles, install/uninstall and dependency checks for one game install.

Handles SACore mods (sacore/mods/<id>/mod.toml), ASI plugins (*.asi in the game folder and scripts/), CLEO scripts
(cleo/*.cs) and Modloader folders (modloader/<name>/).

Disabled items are moved under sacore/disabled/ (outside every folder a loader scans, including the ASI loader's
recursive scripts/ search). Every change first copies the affected files to sacore/backups/<timestamp>/ and is a
dry-run unless --apply is given. Nothing is ever deleted: uninstall moves the mod into the backup folder.

Usage:
    python tools/modman.py --game <dir> list
    python tools/modman.py --game <dir> check
    python tools/modman.py --game <dir> [--apply] disable <item>     # item: mod:<id>, asi:<path>, cleo:<file>, modloader:<name>
    python tools/modman.py --game <dir> [--apply] enable <item>
    python tools/modman.py --game <dir> profile save <name>
    python tools/modman.py --game <dir> [--apply] profile apply <name>
    python tools/modman.py --game <dir> [--apply] install <mod.zip>
    python tools/modman.py --game <dir> [--apply] uninstall <mod id>
"""
from __future__ import annotations

import argparse
import datetime
import re
import shutil
import sys
import tomllib
import zipfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common.packaging import extract_planned, plan_extract  # noqa: E402

CORE_API_VERSION = 1
PROTECTED_ASI = {"scripts/SACore.asi"}  # the framework itself is managed by the installer, not here


@dataclass
class Item:
    key: str        # "mod:<id>", "asi:<relative path>", "cleo:<file>", "modloader:<name>"
    enabled: bool
    path: Path      # current location
    detail: str = ""


def parse_version(text: str) -> tuple[int, int, int] | None:
    match = re.fullmatch(r"\s*(\d+)(?:\.(\d+))?(?:\.(\d+))?\s*", text)
    if not match:
        return None
    return tuple(int(part or 0) for part in match.groups())  # type: ignore[return-value]


def satisfies(version: tuple[int, int, int], requirement: str) -> bool:
    """Same grammar as the core (src/core/version.h): *, =, >, >=, <, <=, ^, ~ joined by commas."""
    for part in (p.strip() for p in requirement.split(",")):
        if part in ("", "*"):
            continue
        if part[0] in "^~":
            base = parse_version(part[1:])
            if base is None:
                return False
            upper = (base[0], base[1] + 1, 0) if part[0] == "~" or base[0] == 0 else (base[0] + 1, 0, 0)
            if not (base <= version < upper):
                return False
            continue
        op = next((o for o in (">=", "<=", ">", "<", "=") if part.startswith(o)), "=")
        target = parse_version(part[len(op):] if part.startswith(op) else part)
        if target is None:
            return False
        ok = {">=": version >= target, "<=": version <= target, ">": version > target, "<": version < target,
              "=": version == target}[op]
        if not ok:
            return False
    return True


class Install:
    def __init__(self, game: Path):
        self.game = game.resolve()
        self.sacore = self.game / "sacore"
        self.disabled = self.sacore / "disabled"

    # ---- discovery -------------------------------------------------------------------------------------------
    def items(self) -> list[Item]:
        found: list[Item] = []
        for root, enabled in ((self.sacore / "mods", True), (self.disabled / "mods", False)):
            for manifest in sorted(root.glob("*/mod.toml")):
                # Keyed by folder name (the core uses the same folder name for its directory); the id is shown.
                data = self._manifest(manifest)
                detail = (f"{data.get('version', '?')} (id {data.get('id', '?')})" if data else "invalid mod.toml")
                found.append(Item(f"mod:{manifest.parent.name}", enabled, manifest.parent, detail))
        for asi in sorted(list(self.game.glob("*.asi")) + list((self.game / "scripts").rglob("*.asi"))):
            rel = asi.relative_to(self.game).as_posix()
            if rel not in PROTECTED_ASI:
                found.append(Item(f"asi:{rel}", True, asi))
        for asi in sorted((self.disabled / "asi").rglob("*.asi")):
            found.append(Item(f"asi:{asi.relative_to(self.disabled / 'asi').as_posix()}", False, asi))
        for cleo in sorted((self.game / "cleo").glob("*.cs")):
            found.append(Item(f"cleo:{cleo.name}", True, cleo))
        for cleo in sorted((self.disabled / "cleo").glob("*.cs")):
            found.append(Item(f"cleo:{cleo.name}", False, cleo))
        for folder in sorted(p for p in (self.game / "modloader").glob("*") if p.is_dir() and not p.name.startswith(".")):
            found.append(Item(f"modloader:{folder.name}", True, folder))
        for folder in sorted(p for p in (self.disabled / "modloader").glob("*") if p.is_dir()):
            found.append(Item(f"modloader:{folder.name}", False, folder))
        return found

    @staticmethod
    def _manifest(path: Path) -> dict | None:
        try:
            return tomllib.loads(path.read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError):
            return None

    def find(self, key: str) -> Item | None:
        if ":" not in key:
            key = f"mod:{key}"
        return next((i for i in self.items() if i.key == key), None)

    # ---- locations -------------------------------------------------------------------------------------------
    def enabled_path(self, key: str) -> Path:
        kind, name = key.split(":", 1)
        return {"mod": self.sacore / "mods" / name, "asi": self.game / name, "cleo": self.game / "cleo" / name,
                "modloader": self.game / "modloader" / name}[kind]

    def disabled_path(self, key: str) -> Path:
        kind, name = key.split(":", 1)
        return self.disabled / {"mod": "mods", "asi": "asi", "cleo": "cleo", "modloader": "modloader"}[kind] / name

    # ---- checks ----------------------------------------------------------------------------------------------
    def check(self) -> list[str]:
        problems: list[str] = []
        mods = {}
        for item in self.items():
            if item.key.startswith("mod:") and item.enabled:
                data = self._manifest(item.path / "mod.toml")
                if data is None:
                    problems.append(f"{item.key}: mod.toml is not valid TOML")
                    continue
                data["_folder"] = item.path
                mods[data.get("id", item.path.name)] = data
        for mod_id, data in mods.items():
            if not re.fullmatch(r"[a-z0-9_.-]{1,64}", str(mod_id)):
                problems.append(f"{data['_folder'].name}: invalid mod id '{mod_id}'")
            if data.get("api_version") != CORE_API_VERSION:
                problems.append(f"mod:{mod_id}: built for mod API {data.get('api_version')}, core provides {CORE_API_VERSION}")
            if data.get("dll") and not (data["_folder"] / data["dll"]).is_file():
                problems.append(f"mod:{mod_id}: {data['dll']} is missing")
            for dep, requirement in data.get("dependencies", {}).items():
                if dep not in mods:
                    problems.append(f"mod:{mod_id}: requires '{dep}' {requirement}, which is not enabled")
                    continue
                version = parse_version(str(mods[dep].get("version", "")))
                if version is None or not satisfies(version, requirement):
                    problems.append(f"mod:{mod_id}: requires '{dep}' {requirement}, found {mods[dep].get('version')}")
        return problems

    # ---- changes ---------------------------------------------------------------------------------------------
    def plan_toggle(self, key: str, enable: bool) -> list[tuple[Path, Path]]:
        item = self.find(key)
        if item is None:
            raise SystemExit(f"unknown item '{key}' (see 'list')")
        if item.enabled == enable:
            return []
        source = item.path
        target = self.enabled_path(item.key) if enable else self.disabled_path(item.key)
        if target.exists():
            raise SystemExit(f"cannot move {source} to {target}: target exists")
        return [(source, target)]

    def apply_moves(self, moves: list[tuple[Path, Path]], label: str) -> Path:
        backup = self.sacore / "backups" / f"{datetime.datetime.now():%Y-%m-%d_%H-%M-%S}-{label}"
        for source, _ in moves:
            destination = backup / source.relative_to(self.game)
            destination.parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, destination)
            else:
                shutil.copy2(source, destination)
        for source, target in moves:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(source), str(target))
        return backup

    def profile_path(self, name: str) -> Path:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            raise SystemExit("profile names may contain only letters, digits, '_', '-' and '.'")
        return self.sacore / "profiles" / f"{name}.toml"


def print_moves(moves: list[tuple[Path, Path]], game: Path) -> None:
    if not moves:
        print("nothing to change")
    for source, target in moves:
        print(f"move {source.relative_to(game).as_posix()} -> {target.relative_to(game).as_posix()}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--apply", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")
    sub.add_parser("check")
    for name in ("enable", "disable"):
        sub.add_parser(name).add_argument("item")
    profile = sub.add_parser("profile")
    profile.add_argument("action", choices=["save", "apply", "show"])
    profile.add_argument("name")
    sub.add_parser("install").add_argument("archive", type=Path)
    sub.add_parser("uninstall").add_argument("mod_id")
    args = parser.parse_args(argv)

    install = Install(args.game)
    if "steamapps" in str(install.game).lower():
        print("REFUSED: manage mods in a test or play copy, not in the Steam folder")
        return 2
    if not any((install.game / e).is_file() for e in ("gta_sa.exe", "gta-sa.exe")):
        print(f"REFUSED: no GTA SA executable in {install.game}")
        return 2

    if args.command == "list":
        for item in install.items():
            print(f"{'on ' if item.enabled else 'off'}  {item.key:<48} {item.detail}")
        return 0
    if args.command == "check":
        problems = install.check()
        for problem in problems:
            print(f"PROBLEM {problem}")
        print("no problems found" if not problems else f"{len(problems)} problem(s)")
        return 0 if not problems else 1

    moves: list[tuple[Path, Path]] = []
    label = args.command
    if args.command in ("enable", "disable"):
        moves = install.plan_toggle(args.item, args.command == "enable")
    elif args.command == "profile":
        path = install.profile_path(args.name)
        if args.action == "save":
            enabled = [i.key for i in install.items() if i.enabled]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("enabled = [\n" + "".join(f'    "{k}",\n' for k in enabled) + "]\n", encoding="utf-8")
            print(f"saved {len(enabled)} enabled item(s) to {path}")
            return 0
        if not path.is_file():
            raise SystemExit(f"no profile '{args.name}'")
        wanted = set(tomllib.loads(path.read_text(encoding="utf-8")).get("enabled", []))
        if args.action == "show":
            print("\n".join(sorted(wanted)))
            return 0
        label = f"profile-{args.name}"
        for item in install.items():
            moves += install.plan_toggle(item.key, item.key in wanted)
    elif args.command == "install":
        with zipfile.ZipFile(args.archive) as archive:
            names = archive.namelist()
            manifest_name = next((n for n in names if n.endswith("mod.toml") and n.count("/") <= 1), None)
            if manifest_name is None:
                raise SystemExit("archive has no mod.toml at its root or in one top-level folder")
            data = tomllib.loads(archive.read(manifest_name).decode("utf-8"))
            prefix = manifest_name[: -len("mod.toml")]
            mod_id = data.get("id", "")
            if not re.fullmatch(r"[a-z0-9_.-]{1,64}", mod_id):
                raise SystemExit(f"invalid mod id '{mod_id}'")
            if data.get("api_version") != CORE_API_VERSION:
                raise SystemExit(f"mod built for API {data.get('api_version')}, this core provides {CORE_API_VERSION}")
            if data.get("dll") and f"{prefix}{data['dll']}" not in names:
                raise SystemExit(f"archive does not contain {data['dll']}")
            members, unsafe = plan_extract(archive, prefix)
            if unsafe:
                raise SystemExit(f"unsafe path in archive: {unsafe[0]}")
            target = install.sacore / "mods" / mod_id
            print(f"install {mod_id} {data.get('version')} into {target}" + (" (replacing the current version)" if target.exists() else ""))
            if not args.apply:
                print("Dry-run only. Re-run with --apply.")
                return 0
            if target.exists():
                print(f"previous version backed up to {install.apply_moves([(target, install.sacore / 'backups' / 'replaced' / mod_id)], 'install')}")
            extract_planned(archive, members, target)
            print("installed")
            return 0
    elif args.command == "uninstall":
        item = install.find(args.mod_id)
        if item is None or not item.key.startswith("mod:"):
            raise SystemExit(f"no SACore mod '{args.mod_id}'")
        moves = [(item.path, install.sacore / "backups" / "uninstalled" /
                  f"{item.key[4:]}-{datetime.datetime.now():%Y%m%d%H%M%S}")]

    print_moves(moves, install.game)
    if not args.apply:
        print("Dry-run only. Re-run with --apply.")
        return 0
    if moves:
        backup = install.apply_moves(moves, label)
        print(f"done; backup of the previous state in {backup}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
