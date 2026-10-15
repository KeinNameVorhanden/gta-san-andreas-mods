"""SACore installer: install, update, verify, uninstall and recover SACore in a GTA San Andreas play copy.

Runs from an unpacked SACore package (this file next to manifest.json and payload/). Standard library only.
Every command is a dry-run that prints its plan unless --apply is given. Nothing is ever deleted: files that
are replaced or removed are moved to <game>/sacore/install/backups/<time>/ first, so every step can be undone.
Saves, settings, logs and crash reports under <game>/sacore are never touched.

Usage:
    python install.py --game <play copy> install [--with devtools,session_stats] [--apply]
    python install.py --game <play copy> update [--apply]          # same package contents over an older install
    python install.py --game <play copy> verify
    python install.py --game <play copy> uninstall [--apply]
    python install.py --game <play copy> recover safe-mode|normal|rollback [--apply]

Refuses: the original Steam folder (path contains "steamapps") unless --allow-steam-folder, an executable that is
not one of the supported builds unless --force-unknown-build, and a running game.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import time
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
STATE = Path("sacore/install/installed.json")
BACKUPS = Path("sacore/install/backups")
CONFIG = Path("sacore/config/core.toml")
EXES = ("gta_sa.exe", "gta-sa.exe")
# Files of other tools that are known to break this setup (docs/COMPATIBILITY.md).
KNOWN_BAD = {
    "modloader.asi": "Modloader 0.3.10 does not work on either supported build; use SACore content mods",
    "scripts/III.VC.SA.WindowedMode.asi": "replaced by SACore's own windowed mode; both together fight over the window",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def exe_identity(path: Path) -> str:
    """SHA-256 of the game executable with the Large Address Aware flag cleared: a copy with the 4 GB flag set
    (tools/laa.py) is still its build."""
    data = bytearray(path.read_bytes())
    if data[:2] == b"MZ" and len(data) > 0x40:
        pe = int.from_bytes(data[0x3C:0x40], "little")
        if data[pe:pe + 4] == b"PE\0\0":
            data[pe + 4 + 18] &= ~0x20 & 0xFF
    return hashlib.sha256(data).hexdigest()


class Installer:
    def __init__(self, package: Path, game: Path, apply: bool, out=print):
        self.package = package
        self.manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
        self.game = game.resolve()
        self.apply = apply
        self.out = out
        self.stamp = time.strftime("%Y%m%d-%H%M%S")

    # ---- shared checks -------------------------------------------------------------------------------------
    def exe(self) -> Path | None:
        return next((self.game / name for name in EXES if (self.game / name).is_file()), None)

    def build_id(self) -> str | None:
        exe = self.exe()
        if exe is None:
            return None
        digest = exe_identity(exe)
        return next((b["id"] for b in self.manifest["known_builds"] if b["sha256"] == digest), None)

    def game_running(self) -> bool:
        exe = self.exe()
        if not os.access(exe, os.W_OK):
            return False  # read-only attribute: cannot tell, and a write lock is what running would look like
        try:
            with exe.open("r+b"):
                return False
        except PermissionError:
            return True

    def preflight(self, allow_steam: bool, force_unknown: bool) -> str | None:
        """Returns the build id (or "unknown"), or None after printing why the command is refused."""
        if self.exe() is None:
            self.out(f"REFUSED: no gta_sa.exe or gta-sa.exe in {self.game}")
            return None
        if "steamapps" in str(self.game).lower() and not allow_steam:
            self.out("REFUSED: this is the Steam folder. Install into a copy of it (docs/INSTALL.md, step 1), "
                     "or pass --allow-steam-folder if you really mean it.")
            return None
        if self.game_running():
            self.out("REFUSED: the game is running. Close it first.")
            return None
        build = self.build_id()
        if build is None:
            if not force_unknown:
                self.out("REFUSED: the game executable is not a supported build "
                         f"({', '.join(b['id'] for b in self.manifest['known_builds'])}). SACore would stay inactive. "
                         "Pass --force-unknown-build to install anyway.")
                return None
            build = "unknown"
        return build

    def state(self) -> dict | None:
        path = self.game / STATE
        return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None

    def save_state(self, state: dict) -> None:
        path = self.game / STATE
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")

    def backup_dir(self, suffix: str = "") -> Path:
        return self.game / BACKUPS / (self.stamp + suffix)

    def move_to(self, rel: str, target_root: Path) -> None:
        source = self.game / rel
        target = target_root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(source), str(target))

    # ---- install / update ----------------------------------------------------------------------------------
    def selected_files(self, groups: set[str]) -> dict[str, dict]:
        return {f["path"]: f for f in self.manifest["files"] if f["group"] == "base" or f["group"] in groups}

    def install(self, groups: set[str], allow_steam: bool, force_unknown: bool, update: bool) -> int:
        build = self.preflight(allow_steam, force_unknown)
        if build is None:
            return 2
        old = self.state()
        if update and old is None:
            self.out("REFUSED: SACore is not installed here (no sacore/install/installed.json); use install.")
            return 2
        if old is not None and not update:
            self.out(f"SACore {old['version']} is already installed here; use update.")
            return 2
        unknown = groups - set(self.manifest["optional_groups"])
        if unknown:
            self.out(f"unknown optional component(s): {', '.join(sorted(unknown))}; "
                     f"available: {', '.join(self.manifest['optional_groups'])}")
            return 2
        if update:
            groups = groups or set(old.get("groups", []))
        wanted = self.selected_files(groups)
        previous = (old or {}).get("files", {})
        originals = dict((old or {}).get("originals", {}))

        copies, replaced, created, retired = [], [], [], []
        for rel, entry in wanted.items():
            target = self.game / rel
            if target.is_file() and sha256(target) == entry["sha256"]:
                continue
            copies.append(rel)
            if target.exists():
                replaced.append(rel)
                if rel not in previous and rel not in originals:
                    originals[rel] = None  # filled with its backup location below
            else:
                created.append(rel)
        retired = [rel for rel in previous if rel not in wanted and (self.game / rel).is_file()]

        self.out(f"{'update' if update else 'install'} SACore {self.manifest['version']} into {self.game} "
                 f"(build {build}{', previous ' + old['version'] if old else ''})")
        for rel in copies:
            self.out(f"  {'replace' if rel in replaced else 'add':8} {rel}")
        for rel in retired:
            self.out(f"  retire   {rel} (no longer part of the selection)")
        config_needed = not (self.game / CONFIG).exists()
        if config_needed:
            self.out(f"  create   {CONFIG.as_posix()} ({self.initial_config_note(build)})")
        for rel, reason in KNOWN_BAD.items():
            if (self.game / rel).exists():
                self.out(f"  WARNING  {rel}: {reason}")
        if not copies and not retired and not config_needed:
            self.out("nothing to do: everything is up to date")
            return 0
        if not self.apply:
            self.out("Dry-run only. Re-run with --apply.")
            return 0

        backup = self.backup_dir()
        for rel in replaced + retired:
            self.move_to(rel, backup)
            if rel in originals and originals[rel] is None:
                originals[rel] = (backup / rel).relative_to(self.game).as_posix()
        for rel in copies:
            target = self.game / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.package / "payload" / rel, target)
        if config_needed:
            (self.game / CONFIG).parent.mkdir(parents=True, exist_ok=True)
            (self.game / CONFIG).write_text(self.initial_config(build), encoding="utf-8")
        history = list((old or {}).get("history", []))
        history.append({"time": self.stamp, "from_version": old["version"] if old else None,
                        "to_version": self.manifest["version"], "backup": (backup.relative_to(self.game)).as_posix(),
                        "replaced": replaced + retired, "created": created,
                        "previous_files": previous, "previous_groups": (old or {}).get("groups", [])})
        self.save_state({"version": self.manifest["version"], "build": build, "groups": sorted(groups),
                         "files": {rel: wanted[rel]["sha256"] for rel in wanted}, "originals": originals,
                         "history": history})
        self.out(f"done. Backup of replaced files: {backup}" if replaced or retired else "done.")
        if build == "us10_hoodlum" and config_needed:
            self.out("Saves of this copy live in sacore/userdata (1.0 US saves differ from the Steam version).")
        return 0

    @staticmethod
    def initial_config_note(build: str) -> str:
        return ("separate User Files for this copy" if build == "us10_hoodlum"
                else "defaults; saves stay in Documents, shared with the Steam game")

    @staticmethod
    def initial_config(build: str) -> str:
        # Only the values that differ from the core defaults; SACore adds every other key with comments on start.
        text = "# Written by the SACore installer. SACore completes this file with all settings on first start.\n"
        if build == "us10_hoodlum":
            text += "\n[saves]\nseparate_user_files = true\n"
        return text

    # ---- verify ----------------------------------------------------------------------------------------------
    def verify(self) -> int:
        problems = 0
        build = self.build_id() if self.exe() else None
        self.out(f"game: {self.game}")
        self.out(f"build: {build or 'NOT SUPPORTED (SACore stays inactive)'}")
        problems += build is None
        state = self.state()
        if state is None:
            self.out("SACore: not installed by this installer")
            return 1
        self.out(f"SACore: {state['version']} (optional: {', '.join(state.get('groups', [])) or 'none'})")
        for rel, digest in state["files"].items():
            path = self.game / rel
            if not path.is_file():
                self.out(f"  MISSING  {rel}")
                problems += 1
            elif sha256(path) != digest:
                self.out(f"  CHANGED  {rel} (differs from the installed file)")
                problems += 1
        config = self.game / CONFIG
        if config.is_file():
            try:
                tomllib.loads(config.read_text(encoding="utf-8"))
                self.out(f"  ok       {CONFIG.as_posix()} parses")
            except tomllib.TOMLDecodeError as error:
                self.out(f"  BROKEN   {CONFIG.as_posix()}: {error} (SACore will use defaults and keep a .broken copy)")
                problems += 1
        for rel, reason in KNOWN_BAD.items():
            if (self.game / rel).exists():
                self.out(f"  WARNING  {rel}: {reason}")
                problems += 1
        ours = set(state["files"])
        for asi in sorted(self.game.glob("*.asi")) + sorted((self.game / "scripts").rglob("*.asi")):
            rel = asi.relative_to(self.game).as_posix()
            if rel not in ours and rel not in KNOWN_BAD:
                self.out(f"  note     other plugin: {rel}")
        self.out("verify: OK" if problems == 0 else f"verify: {problems} problem(s)")
        return 0 if problems == 0 else 1

    # ---- uninstall -------------------------------------------------------------------------------------------
    def uninstall(self) -> int:
        state = self.state()
        if state is None:
            self.out("SACore is not installed here (no sacore/install/installed.json).")
            return 2
        if self.exe() and self.game_running():
            self.out("REFUSED: the game is running. Close it first.")
            return 2
        removed = self.backup_dir("-uninstall")
        moves, kept, restores = [], [], []
        for rel, digest in state["files"].items():
            path = self.game / rel
            if not path.is_file():
                continue
            (moves if sha256(path) == digest else kept).append(rel)
        for rel, backup in state.get("originals", {}).items():
            if backup and (self.game / backup).is_file():
                restores.append((rel, backup))
        self.out(f"uninstall SACore {state['version']} from {self.game}")
        for rel in moves:
            self.out(f"  remove   {rel} (moved to {removed.relative_to(self.game).as_posix()})")
        for rel in kept:
            self.out(f"  keep     {rel} (changed since install; remove it yourself if wanted)")
        for rel, backup in restores:
            self.out(f"  restore  {rel} (the file that was there before SACore)")
        self.out("  keep     sacore/ settings, logs, crash reports, saves (sacore/userdata) and save backups")
        if not self.apply:
            self.out("Dry-run only. Re-run with --apply.")
            return 0
        for rel in moves:
            self.move_to(rel, removed)
        for rel, backup in restores:
            target = self.game / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(self.game / backup), str(target))
        removed.mkdir(parents=True, exist_ok=True)
        shutil.move(str(self.game / STATE), str(removed / "installed.json"))
        self.out(f"done. Removed files are in {removed}")
        if (self.game / "sacore" / "userdata").is_dir():
            self.out("Saves made with separate User Files stay in sacore/userdata; the game without SACore uses "
                     "Documents/GTA San Andreas User Files again.")
        return 0

    # ---- recover ---------------------------------------------------------------------------------------------
    def set_mods_enabled(self, enabled: bool) -> int:
        path = self.game / CONFIG
        text = path.read_text(encoding="utf-8") if path.is_file() else ""
        lines = text.splitlines()
        value = "true" if enabled else "false"
        section = None
        replaced = False
        insert_at = None
        for index, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                section = stripped.strip("[]").strip()
                if section == "mods":
                    insert_at = index + 1
                continue
            key = stripped.split("=", 1)[0].strip() if "=" in stripped and not stripped.startswith("#") else None
            if section == "mods" and key == "enabled":
                lines[index] = f"enabled = {value}"
                replaced = True
        if not replaced:
            if insert_at is None:
                lines += ["", "[mods]", f"enabled = {value}"]
            else:
                lines.insert(insert_at, f"enabled = {value}")
        self.out(f"set [mods] enabled = {value} in {CONFIG.as_posix()} "
                 f"({'SACore runs without any mod' if not enabled else 'mods load again'})")
        if not self.apply:
            self.out("Dry-run only. Re-run with --apply.")
            return 0
        if path.is_file():
            backup = self.backup_dir("-config")
            backup.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, backup / "core.toml")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        self.out("done")
        return 0

    def rollback(self) -> int:
        state = self.state()
        if state is None or not state.get("history"):
            self.out("nothing to roll back (no installer history here)")
            return 2
        if self.game_running():
            self.out("REFUSED: the game is running. Close it first.")
            return 2
        last = state["history"][-1]
        if last["from_version"] is None:
            self.out("the last change was the first install; use uninstall instead")
            return 2
        undo = self.backup_dir("-rollback")
        self.out(f"roll back {last['to_version']} -> {last['from_version']} (installed {last['time']})")
        for rel in last["created"]:
            self.out(f"  remove   {rel} (moved to {undo.relative_to(self.game).as_posix()})")
        for rel in last["replaced"]:
            self.out(f"  restore  {rel}")
        if not self.apply:
            self.out("Dry-run only. Re-run with --apply.")
            return 0
        for rel in last["created"] + last["replaced"]:
            if (self.game / rel).is_file():
                self.move_to(rel, undo)
        for rel in last["replaced"]:
            shutil.move(str(self.game / last["backup"] / rel), str(self.game / rel))
        # A foreign file first replaced by this update is back in place, so it is no longer an original to restore.
        backup_prefix = last["backup"] + "/"
        state["originals"] = {rel: where for rel, where in state.get("originals", {}).items()
                              if not (where or "").startswith(backup_prefix)}
        state["files"] = last["previous_files"]
        state["groups"] = last["previous_groups"]
        state["version"] = last["from_version"]
        state["history"] = state["history"][:-1]
        self.save_state(state)
        self.out(f"done. Files of {last['to_version']} are in {undo}")
        return 0


def main(argv: list[str], package: Path = HERE, out=print) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", required=True, type=Path, help="the play copy (folder with gta_sa.exe)")
    parser.add_argument("--apply", action="store_true", help="carry out the plan (default: dry-run)")
    parser.add_argument("--allow-steam-folder", action="store_true")
    parser.add_argument("--force-unknown-build", action="store_true")
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("install", "update"):
        sub = commands.add_parser(name)
        sub.add_argument("--with", dest="groups", default="", help="optional components, comma-separated")
    commands.add_parser("verify")
    commands.add_parser("uninstall")
    recover = commands.add_parser("recover")
    recover.add_argument("action", choices=["safe-mode", "normal", "rollback"])
    args = parser.parse_args(argv)

    if not args.game.is_dir():
        out(f"REFUSED: {args.game} is not a folder")
        return 2
    installer = Installer(package, args.game, args.apply, out)
    if args.command in ("install", "update"):
        groups = {g.strip() for g in args.groups.split(",") if g.strip()}
        return installer.install(groups, args.allow_steam_folder, args.force_unknown_build, args.command == "update")
    if args.command == "verify":
        return installer.verify()
    if args.command == "uninstall":
        return installer.uninstall()
    if args.action == "rollback":
        return installer.rollback()
    return installer.set_mods_enabled(args.action == "normal")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
