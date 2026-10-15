"""Safe archive handling for mod and release packages (game-agnostic)."""
from __future__ import annotations

import zipfile
from pathlib import Path, PurePosixPath, PureWindowsPath


def safe_relative(name: str) -> PurePosixPath | None:
    """The archive member name as a relative path, or None if it could leave the target folder.

    Rejects absolute paths, drive letters (also drive-relative 'C:x'), UNC prefixes and '..' parts, in both
    slash styles, because a zip written on any system can carry any of them.
    """
    if not name or "\0" in name:
        return None
    windows = PureWindowsPath(name)
    if windows.drive or windows.root or windows.anchor:
        return None
    posix = PurePosixPath(name.replace("\\", "/"))
    if posix.is_absolute() or ".." in posix.parts:
        return None
    return posix


def plan_extract(archive: zipfile.ZipFile, prefix: str = "") -> tuple[list[tuple[str, PurePosixPath]], list[str]]:
    """Files below prefix with their relative target paths, and the unsafe member names (checked up front, so a
    bad archive is refused before anything is written)."""
    files, unsafe = [], []
    for name in archive.namelist():
        if not name.startswith(prefix) or name.endswith("/"):
            continue
        rel = safe_relative(name[len(prefix):])
        if rel is None:
            unsafe.append(name)
        else:
            files.append((name, rel))
    return files, unsafe


def extract_planned(archive: zipfile.ZipFile, files: list[tuple[str, PurePosixPath]], target: Path) -> None:
    for name, rel in files:
        destination = target / Path(*rel.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(archive.read(name))


def zip_folder(folder: Path, archive: Path, root_name: str | None = None) -> int:
    """Writes every file below folder into archive (deterministic order); returns the file count."""
    count = 0
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(p for p in folder.rglob("*") if p.is_file()):
            rel = path.relative_to(folder).as_posix()
            zf.write(path, f"{root_name}/{rel}" if root_name else rel)
            count += 1
    return count
