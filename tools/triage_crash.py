"""Triage a SACore crash package: what crashed, where, the call chain with names, and what to look at next.

Input: the <time>.zip from <game>\\sacore\\crashes (or its extracted folder, or the .txt report).
Names come from:
    SACore.asi / mod DLLs   linker map of exactly that binary (symbols/<sha8>/ archive, else the current build)
    gta_sa.exe (1.0 US)     tools/data/names_us10.json (plugin-sdk) and SACore's adapter symbols
    gta-sa.exe (newsteam)   SACore's adapter symbols; other functions get their start address for Ghidra
Function starts are found in a local copy of the matching game executable, which also gives the disassembly around
the fault when capstone is installed. That copy is --exe, or else found by hash in the game copies below the folders
listed in the environment variable SACORE_GAME_COPIES (separated like PATH; each folder holds one copy per
subfolder).

Usage:
    python tools/triage_crash.py <crash.zip | folder | report.txt> [--exe <game exe>] [--out triage.md]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from common import minidump  # noqa: E402
from common.packaging import extract_planned, plan_extract  # noqa: E402
import laa  # noqa: E402

SYMBOLS = REPO / "symbols"
BUILD = REPO / "build" / "x86"
KNOWN_BUILDS = REPO / "tools" / "data" / "known_builds.json"
NAMES_US10 = REPO / "tools" / "data" / "names_us10.json"
ADAPTERS = REPO / "src" / "adapters"
EXE_SEARCH = [Path(p) for p in os.environ.get("SACORE_GAME_COPIES", "").split(os.pathsep) if p]
GAME_MODULES = {"gta_sa.exe", "gta-sa.exe"}
PREFERRED_BASE = 0x400000
# Functions SACore's own analysis identified (docs/ADDRESSES.md), as RVAs.
SUPPLEMENT = {
    "us10_hoodlum": {0x348710: "WinMain", 0x173330: "CMenuManager::DoSettingsBeforeStartingAGame",
                     0x401D50: "RwFreeListFree", 0x3C4770: "RpHAnim frame destructor (plugin callback)"},
    "newsteam_r2_lv": {0x37CD60: "WinMain", 0x186E90: "CMenuManager::DoSettingsBeforeStartingAGame",
                       0x436480: "RwFreeListFree", 0x3F8E70: "RpHAnim frame destructor (plugin callback)"},
}
ADDRESS = re.compile(r"([\w.\-]+\.(?:exe|asi|dll|EXE|DLL))\+0x([0-9a-fA-F]+)")


@dataclass
class Report:
    text: str
    exception: str = ""
    fault: str = ""
    access: str = ""
    stack: list[str] = field(default_factory=list)
    scan: list[str] = field(default_factory=list)
    build: str = ""
    core_sha: str = ""
    context: list[str] = field(default_factory=list)
    log: list[str] = field(default_factory=list)


def parse_report(text: str) -> Report:
    r = Report(text=text)
    section = None
    for line in text.splitlines():
        if line.startswith("Exception:"):
            r.exception = line.split(":", 1)[1].strip()
        elif line.startswith("Faulting address:"):
            r.fault = line.split(":", 1)[1].strip()
        elif line.startswith("Access:"):
            r.access = line.split(":", 1)[1].strip()
        elif line.startswith("Stack (innermost"):
            section = "stack"
        elif line.startswith("Stack scan"):
            section = "scan"
        elif line.startswith(("Registers:", "System:", "Minidump", "No minidump", "Send ", "Next steps")):
            section = None
        elif line.startswith("Context:"):
            section = "context"
        elif line.startswith("Last log lines:"):
            section = "log"
        elif section and line.startswith("  ") and line.strip():
            value = line.strip()
            if section in ("stack", "scan"):
                getattr(r, section).append(value.split(None, 1)[1] if " " in value else value)
            elif section == "context":
                r.context.append(value)
                if m := re.match(r"SACore \S+ \(mod API \d+\), build (\S+)", value):
                    r.build = m.group(1)
                if value.startswith("SACore.asi sha256 "):
                    r.core_sha = value.split()[-1]
            elif section == "log":
                r.log.append(value)
    return r


# ---- names ------------------------------------------------------------------------------------------------------

def load_map(path: Path) -> tuple[int, list[tuple[int, str]]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    base = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", text).group(1), 16)
    symbols = sorted((int(m.group(2), 16), m.group(1)) for m in
                     re.finditer(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+f?\s", text, re.M))
    return base, symbols


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find_map(module: str, core_sha: str) -> tuple[Path | None, str]:
    stem = Path(module).stem
    if core_sha:
        archived = SYMBOLS / core_sha[:8] / f"{stem}.map"
        if archived.is_file():
            return archived, "archived symbols of this exact binary"
    candidates = list(BUILD.rglob(f"Release/{stem}.map"))
    if not candidates:
        return None, "no map file"
    note = "current build"
    if module.lower() == "sacore.asi" and core_sha:
        built = candidates[0].with_suffix(".asi")
        if not built.is_file():
            note += " (binary missing: cannot verify it is the crashed one)"
        elif sha256(built) == core_sha:
            note += " (SAME binary)"
        else:
            note += " (DIFFERENT binary than the crashed one: names may be off)"
    return candidates[0], note


def adapter_symbols() -> dict[str, dict[int, str]]:
    """RVA -> name per build id, read from src/adapters (the single source of the addresses SACore uses)."""
    header = (ADAPTERS / "adapters.h").read_text(encoding="utf-8")
    names = dict(re.findall(r"inline constexpr std::string_view (k\w+) = \"([^\"]+)\"", header))
    source = (ADAPTERS / "adapters.cpp").read_text(encoding="utf-8")
    result: dict[str, dict[int, str]] = {}
    for build, block in (("newsteam_r2_lv", "kNewsteamSymbols"), ("us10_hoodlum", "kUs10Symbols")):
        body = source.split(block, 1)[1].split("};", 1)[0]
        result[build] = {int(rva, 16): names.get(k, k) for k, rva in re.findall(r"\{(k\w+), (0x[0-9A-Fa-f]+)", body)}
    return result


class GameImage:
    """Bytes of a local copy of the crashed game build, addressed by RVA."""

    def __init__(self, path: Path):
        import pefile
        self.path = path
        self.pe = pefile.PE(str(path), fast_load=True)
        self.data = path.read_bytes()

    def bytes_at(self, rva: int, size: int) -> bytes:
        try:
            offset = self.pe.get_offset_from_rva(rva)
        except Exception:
            return b""
        return self.data[offset:offset + size]

    def is_function_start(self, start: int) -> bool:
        """True if the code before `start` ends a function: ret / ret n, then only int3 or nop padding.
        Aligned jump targets inside a function are preceded by padding too, but not by a ret or jmp."""
        before = self.bytes_at(start - 24, 24)
        if len(before) != 24:
            return False
        i = 23
        while i >= 0 and before[i] in (0x90, 0xCC):
            i -= 1
        if i < 0:
            return True
        # Only a return ends a function for sure: unconditional jumps followed by alignment also occur inside
        # functions (switch cases), and would cut WinMain in pieces.
        return before[i] == 0xC3 or (i >= 2 and before[i - 2] == 0xC2)

    def function_start(self, rva: int, limit: int = 0x8000) -> int | None:
        """Heuristic: nearest 16-byte aligned address at or below rva that follows the end of another function."""
        start = rva & ~0xF
        while rva - start <= limit:
            if self.is_function_start(start):
                return start
            start -= 0x10
        return None


def find_game_exe(build: str, explicit: Path | None) -> Path | None:
    if explicit:
        return explicit
    known = {b["id"]: b for b in json.loads(KNOWN_BUILDS.read_text(encoding="utf-8"))["builds"]}
    if build not in known:
        return None
    for root in EXE_SEARCH:
        for exe in sorted(root.glob("*/*.exe")):
            if exe.name.lower() in GAME_MODULES and exe.stat().st_size == known[build]["size"] and \
                    laa.identity(exe.read_bytes())[0] == known[build]["sha256"]:
                return exe
    return None


class Resolver:
    def __init__(self, report: Report, exe: Path | None):
        self.report = report
        self.maps: dict[str, tuple[int, list[tuple[int, str]], str] | None] = {}
        self.adapters = adapter_symbols().get(report.build, {})
        self.names = {}
        if report.build == "us10_hoodlum" and NAMES_US10.is_file():
            self.names = {int(a, 16) - PREFERRED_BASE: n for a, n in
                          json.loads(NAMES_US10.read_text(encoding="utf-8"))["functions"].items()}
        self.image = GameImage(exe) if exe else None
        for rva, name in SUPPLEMENT.get(report.build, {}).items():
            self.names.setdefault(rva, name)
        # Adapter symbols include data and call sites; only real function starts can name a function.
        for rva, name in self.adapters.items():
            if self.image and self.image.is_function_start(rva):
                self.names.setdefault(rva, name)

    def name(self, token: str) -> str:
        m = ADDRESS.search(token)
        if not m:
            return token
        module, offset = m.group(1), int(m.group(2), 16)
        if module.lower() in GAME_MODULES:
            return self.game_name(module, offset)
        if module.lower().endswith((".asi", ".dll")) and not module.lower().startswith(
                ("ntdll", "kernel", "user32", "d3d9", "win32u", "ucrtbase", "msvcp", "vcruntime")):
            return self.map_name(module, offset)
        return f"{module}+{offset:#x}"

    def map_name(self, module: str, offset: int) -> str:
        if module not in self.maps:
            path, note = find_map(module, self.report.core_sha if module.lower() == "sacore.asi" else "")
            self.maps[module] = (*load_map(path), note) if path else None
        entry = self.maps[module]
        if not entry:
            return f"{module}+{offset:#x} (no map file)"
        base, symbols, _ = entry
        address = base + offset
        best = None
        for value, name in symbols:
            if value > address:
                break
            best = (value, name)
        return f"{module}+{offset:#x}  {best[1]}+{address - best[0]:#x}" if best else f"{module}+{offset:#x}"

    def game_name(self, module: str, rva: int) -> str:
        va = PREFERRED_BASE + rva
        start = self.image.function_start(rva) if self.image else None
        named_before = max((a for a in self.names if a <= rva), default=None)
        if named_before is not None and (start is None or named_before > start) and rva - named_before < 0x4000:
            start = named_before  # a known function start is a better boundary than the heuristic
        if start is not None and start in self.names:
            return f"{module}+{rva:#x}  {self.names[start]}+{rva - start:#x}  (VA {va:#x})"
        known_before = max((a for a in self.names if a <= rva), default=None)
        hint = ""
        if known_before is not None and rva - known_before < 0x10000:
            hint = f", after {self.names[known_before]}"
        where = f"function at {PREFERRED_BASE + start:#x}" if start is not None else "unknown function"
        return f"{module}+{rva:#x}  {where} (VA {va:#x}{hint})"


# ---- report -----------------------------------------------------------------------------------------------------

def disassembly(image: GameImage, rva: int) -> list[str]:
    try:
        import capstone
    except ImportError:
        return ["(install capstone for a disassembly: pip install capstone)"]
    start = image.function_start(rva) or rva - 0x20
    start = max(start, rva - 0x60)
    code = image.bytes_at(start, (rva - start) + 0x20)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    return [f"{'=>' if PREFERRED_BASE + rva == i.address else '  '} {i.address:#010x}  {i.mnemonic} {i.op_str}"
            for i in md.disasm(code, PREFERRED_BASE + start)][:40]


def triage(source: Path, exe: Path | None) -> tuple[str, Path]:
    if not source.exists() or str(source) in ("", "."):
        raise SystemExit(f"not found: {source}")
    work = source
    if source.suffix.lower() == ".zip":
        work = Path(tempfile.mkdtemp(prefix="sacore_triage_"))
        with zipfile.ZipFile(source) as archive:
            files, unsafe = plan_extract(archive)
            if unsafe:
                raise SystemExit(f"refused: unsafe paths in the archive: {unsafe}")
            extract_planned(archive, files, work)
    report_file = source if source.suffix.lower() == ".txt" else next(iter(sorted(work.glob("*.txt"))), None)
    if report_file is None:
        raise SystemExit("no crash report (.txt) found")
    report = parse_report(report_file.read_text(encoding="utf-8", errors="replace"))
    game_exe = find_game_exe(report.build, exe)
    resolver = Resolver(report, game_exe)

    out = [f"# Crash triage: {report_file.stem}", ""]
    out += [f"- Exception: {report.exception}", f"- Fault: {resolver.name(report.fault)}"]
    if report.access:
        out.append(f"- Access: {report.access}")
    out += [f"- Build: {report.build or 'unknown'}; game executable for names: {game_exe or 'not found'}"]
    for line in report.context:
        if line.startswith(("SACore ", "session ", "game state")):
            out.append(f"- {line}")
    fault_module = (ADDRESS.search(report.fault) or [None, ""])[1]
    if fault_module and fault_module.lower() not in GAME_MODULES and fault_module.lower() != "sacore.asi" and \
            not fault_module.lower().startswith(("ntdll", "kernel", "ucrtbase")):
        out.append(f"- The fault is inside {fault_module}: if that is a mod or plugin, disable it with "
                   "tools/modman.py and check whether the crash stays.")
    chain = report.scan or report.stack
    out += ["", "## Call chain (innermost first)", ""]
    out += [f"{i:>2}. {resolver.name(t)}" for i, t in enumerate(chain)] or ["(no stack)"]
    if report.stack and report.scan:
        out += ["", "Frame-pointer walk (stops early in game code): " +
                "; ".join(resolver.name(t) for t in report.stack)]
    m = ADDRESS.search(report.fault)
    if m and m.group(1).lower() in GAME_MODULES and resolver.image:
        out += ["", "## Code at the fault", "", "```"] + disassembly(resolver.image, int(m.group(2), 16)) + ["```"]
    dumps = sorted(work.glob("*.dmp")) if work.is_dir() else []
    if dumps:
        try:
            dump = minidump.load(dumps[0])
            main = dump.threads[0].thread_id if dump.threads else None
            where = "the main thread" if dump.exception_thread == main else f"thread {dump.exception_thread} (not the main thread)"
            out += ["", "## Minidump", "",
                    f"- {len(dump.threads)} threads, {len(dump.modules)} modules; the exception happened on {where}",
                    f"- Exception address {dump.describe(dump.exception_address or 0)}"]
        except minidump.NotMinidump as error:
            out += ["", f"Minidump unreadable: {error}"]
    interesting = [line for line in report.log if any(k in line for k in ("[ERROR]", "[WARN]", "[FATAL]"))]
    out += ["", "## Last log lines", "", "```"] + report.log[-15:] + ["```"]
    if interesting:
        out += ["", "Warnings and errors in the log tail:", "", "```"] + interesting[-10:] + ["```"]
    unnamed = [t for t in chain if "function at" in resolver.name(t)]
    if unnamed and report.build:
        program = "gta_sa_us10_hoodlum.exe" if report.build == "us10_hoodlum" else "gta-sa.exe"
        start = re.search(r"function at (0x[0-9a-f]+)", resolver.name(unnamed[0])).group(1)
        out += ["", "## Next step for unnamed game functions", "",
                f"`tools/ghidra/run_script.ps1 -Program {program} -Script AddressXrefDecompile.java "
                f"-ScriptArgs @('at','{start}')`"]
    return "\n".join(out) + "\n", work


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("--exe", type=Path, default=None, help="local copy of the crashed game executable")
    parser.add_argument("--out", type=Path, default=None, help="also write the triage to this file")
    args = parser.parse_args(argv)
    text, _ = triage(args.source, args.exe)
    print(text)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
