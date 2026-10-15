"""Turn "module+0xRVA" from a SACore crash report into a function name using the linker map file.

Usage:
    python tools/symbolize.py build/x86/src/core/Release/SACore.map 0x22707 [0x5f4f9 ...]
"""
from __future__ import annotations

import re
import sys
from pathlib import Path


def load(map_file: Path) -> tuple[int, list[tuple[int, str]]]:
    text = map_file.read_text(encoding="utf-8", errors="replace")
    base = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", text).group(1), 16)
    symbols = []
    for match in re.finditer(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+f?\s", text, re.MULTILINE):
        symbols.append((int(match.group(2), 16), match.group(1)))
    symbols.sort()
    return base, symbols


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    base, symbols = load(Path(argv[0]))
    for text in argv[1:]:
        address = base + int(text, 16)
        best = None
        for value, name in symbols:
            if value > address:
                break
            best = (value, name)
        print(f"{text}: {best[1]}+{address - best[0]:#x}" if best else f"{text}: before the first symbol")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
