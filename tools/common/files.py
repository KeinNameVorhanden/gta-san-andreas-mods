"""File hashing, tree manifests and Windows version resources (game-agnostic)."""
from __future__ import annotations

import ctypes
import hashlib
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_manifest(root: Path) -> dict[str, dict]:
    """{relative posix path: {size, sha256}} for every file below root."""
    return {
        p.relative_to(root).as_posix(): {"size": p.stat().st_size, "sha256": sha256_file(p)}
        for p in sorted(root.rglob("*")) if p.is_file()
    }


def manifest_diff(expected: dict[str, dict], actual: dict[str, dict]) -> list[str]:
    lines = []
    for name in sorted(expected.keys() - actual.keys()):
        lines.append(f"MISSING  {name}")
    for name in sorted(actual.keys() - expected.keys()):
        lines.append(f"ADDED    {name}")
    for name in sorted(expected.keys() & actual.keys()):
        if expected[name]["sha256"] != actual[name]["sha256"]:
            lines.append(f"CHANGED  {name}")
    return lines


def file_version(path: Path, field: str = "ProductVersion") -> str | None:
    """A string from the file's version resource (English/Unicode block), None when absent or not on Windows."""
    if sys.platform != "win32":
        return None
    size = ctypes.windll.version.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return None
    buffer = ctypes.create_string_buffer(size)
    if not ctypes.windll.version.GetFileVersionInfoW(str(path), 0, size, buffer):
        return None
    value = ctypes.c_void_p()
    length = ctypes.c_uint()
    query = "\\StringFileInfo\\040904b0\\" + field
    if not ctypes.windll.version.VerQueryValueW(buffer, query, ctypes.byref(value), ctypes.byref(length)):
        return None
    return ctypes.wstring_at(value.value, length.value).rstrip("\0")
