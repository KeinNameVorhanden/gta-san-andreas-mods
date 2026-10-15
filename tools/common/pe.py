"""Minimal read-only PE (Windows executable) header reader (game-agnostic, standard library only)."""
from __future__ import annotations

import datetime
import struct
from dataclasses import dataclass, field
from pathlib import Path

MACHINES = {0x14C: "x86", 0x8664: "x64", 0xAA64: "arm64"}
DLL_CHARACTERISTICS_DYNAMIC_BASE = 0x40


@dataclass
class PeInfo:
    machine: str
    timestamp: str
    image_base: int
    aslr: bool
    sections: list[str] = field(default_factory=list)
    imports: list[str] = field(default_factory=list)
    is_dotnet: bool = False
    pdb_path: str | None = None


class NotPe(ValueError):
    pass


def read_pe(path: Path) -> PeInfo:
    try:
        return _read_pe(path.name, path.read_bytes())
    except struct.error as error:
        raise NotPe(f"{path.name}: truncated or malformed headers ({error})") from error


def _read_pe(name: str, data: bytes) -> PeInfo:
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise NotPe(f"{name}: no MZ header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise NotPe(f"{name}: no PE signature")
    machine, section_count, timestamp = struct.unpack_from("<HHI", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    pe32_plus = magic == 0x20B
    image_base = struct.unpack_from("<Q" if pe32_plus else "<I", data, optional + (24 if pe32_plus else 28))[0]
    dll_characteristics = struct.unpack_from("<H", data, optional + 70)[0]
    directories = optional + (112 if pe32_plus else 96)
    sections_offset = optional + optional_size

    sections = []
    for index in range(section_count):
        entry = sections_offset + index * 40
        name = data[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
        sections.append((name, virtual_address, max(virtual_size, raw_size), raw_pointer))

    def to_offset(rva: int) -> int | None:
        for _, address, size, raw in sections:
            if address <= rva < address + size:
                return raw + (rva - address)
        return None

    def directory(index: int) -> tuple[int, int]:
        return struct.unpack_from("<II", data, directories + index * 8)

    def c_string(offset: int) -> str:
        end = data.find(b"\0", offset)
        return data[offset:end].decode("ascii", "replace")

    imports = []
    import_rva, _ = directory(1)
    offset = to_offset(import_rva) if import_rva else None
    while offset is not None and offset + 20 <= len(data):
        name_rva = struct.unpack_from("<I", data, offset + 12)[0]
        if name_rva == 0:
            break
        name_offset = to_offset(name_rva)
        if name_offset is not None:
            imports.append(c_string(name_offset).lower())
        offset += 20

    pdb = None
    debug_rva, debug_size = directory(6)
    debug_offset = to_offset(debug_rva) if debug_rva else None
    if debug_offset is not None:
        for entry in range(debug_offset, debug_offset + debug_size, 28):
            kind, size, _, pointer = struct.unpack_from("<IIII", data, entry + 12)
            if kind == 2 and data[pointer:pointer + 4] == b"RSDS":
                pdb = c_string(pointer + 24)
                break

    clr_rva, _ = directory(14)
    stamp = datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc).strftime("%Y-%m-%d")
    return PeInfo(machine=MACHINES.get(machine, hex(machine)), timestamp=stamp, image_base=image_base,
                  aslr=bool(dll_characteristics & DLL_CHARACTERISTICS_DYNAMIC_BASE),
                  sections=[s[0] for s in sections], imports=imports, is_dotnet=clr_rva != 0, pdb_path=pdb)
