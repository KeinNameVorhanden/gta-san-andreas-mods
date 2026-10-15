"""Read-only minidump reader (x86 and x64 contexts): modules, threads, exception, memory (game-agnostic)."""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

THREAD_LIST, MODULE_LIST, MEMORY_LIST, EXCEPTION, SYSTEM_INFO, MEMORY64_LIST = 3, 4, 5, 6, 7, 9


@dataclass
class Module:
    base: int
    size: int
    name: str


@dataclass
class Thread:
    thread_id: int
    stack_start: int
    stack: bytes
    eip: int
    esp: int
    ebp: int


@dataclass
class Minidump:
    modules: list[Module] = field(default_factory=list)
    threads: list[Thread] = field(default_factory=list)
    exception_thread: int | None = None
    exception_code: int | None = None
    exception_address: int | None = None
    memory: list[tuple[int, int, int]] = field(default_factory=list)  # (start, size, file offset)
    data: bytes = b""

    def module_at(self, address: int) -> Module | None:
        return next((m for m in self.modules if m.base <= address < m.base + m.size), None)

    def describe(self, address: int) -> str:
        module = self.module_at(address)
        return f"{module.name}+{address - module.base:#x}" if module else f"{address:#010x}"

    def read(self, address: int, size: int) -> bytes | None:
        for start, length, offset in self.memory:
            if start <= address and address + size <= start + length:
                return self.data[offset + address - start:offset + address - start + size]
        return None


class NotMinidump(ValueError):
    pass


def load(path: Path) -> Minidump:
    data = path.read_bytes()
    if data[:4] != b"MDMP":
        raise NotMinidump(f"{path.name}: not a minidump")
    try:
        return _parse(data)
    except struct.error as error:
        raise NotMinidump(f"{path.name}: truncated ({error})") from error


def _utf16(data: bytes, rva: int) -> str:
    length = struct.unpack_from("<I", data, rva)[0]
    return data[rva + 4:rva + 4 + length].decode("utf-16-le", "replace")


def _parse(data: bytes) -> Minidump:
    dump = Minidump(data=data)
    count, directory = struct.unpack_from("<II", data, 8)
    streams: dict[int, list[tuple[int, int]]] = {}
    for i in range(count):
        kind, size, rva = struct.unpack_from("<III", data, directory + i * 12)
        streams.setdefault(kind, []).append((size, rva))
    for _, rva in streams.get(MODULE_LIST, []):
        n = struct.unpack_from("<I", data, rva)[0]
        for i in range(n):
            entry = rva + 4 + i * 108
            base, size = struct.unpack_from("<QI", data, entry)
            name = _utf16(data, struct.unpack_from("<I", data, entry + 20)[0]).replace("/", "\\").split("\\")[-1]
            dump.modules.append(Module(base, size, name))
    for _, rva in streams.get(MEMORY_LIST, []):
        n = struct.unpack_from("<I", data, rva)[0]
        for i in range(n):
            start, size, offset = struct.unpack_from("<QII", data, rva + 4 + i * 16)
            dump.memory.append((start, size, offset))
    for _, rva in streams.get(MEMORY64_LIST, []):
        n, offset = struct.unpack_from("<QQ", data, rva)
        for i in range(n):
            start, size = struct.unpack_from("<QQ", data, rva + 16 + i * 16)
            dump.memory.append((start, size, offset))
            offset += size
    for _, rva in streams.get(EXCEPTION, []):
        dump.exception_thread = struct.unpack_from("<I", data, rva)[0]
        dump.exception_code = struct.unpack_from("<I", data, rva + 8)[0]
        dump.exception_address = struct.unpack_from("<Q", data, rva + 8 + 16)[0]
    x64 = any(struct.unpack_from("<H", data, rva)[0] == 9 for _, rva in streams.get(SYSTEM_INFO, []))
    for _, rva in streams.get(THREAD_LIST, []):
        n = struct.unpack_from("<I", data, rva)[0]
        for i in range(n):
            entry = rva + 4 + i * 48
            thread_id = struct.unpack_from("<I", data, entry)[0]
            stack_start, stack_size, stack_rva, context_size, context_rva = struct.unpack_from("<QIIII", data, entry + 24)
            if x64:  # AMD64 CONTEXT: Rsp 0x98, Rbp 0xA0, Rip 0xF8
                esp, ebp = struct.unpack_from("<QQ", data, context_rva + 0x98)
                eip = struct.unpack_from("<Q", data, context_rva + 0xF8)[0]
            elif context_size >= 0xCC:  # x86 CONTEXT: Ebp 0xB4, Eip 0xB8, Esp 0xC4
                ebp, eip = struct.unpack_from("<II", data, context_rva + 0xB4)
                esp = struct.unpack_from("<I", data, context_rva + 0xC4)[0]
            else:
                ebp = eip = esp = 0
            dump.threads.append(Thread(thread_id, stack_start, data[stack_rva:stack_rva + stack_size], eip, esp, ebp))
    return dump
