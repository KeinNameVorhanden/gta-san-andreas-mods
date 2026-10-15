"""Search a PE file's code sections for a byte pattern with wildcards and report matches.

Pattern syntax: hex bytes separated by spaces, '??' = any byte, '{d32}' = capture a 32-bit little-endian value.
Prints each match VA (preferred image base) and the captured values, plus a histogram of captured values.

Usage:
    python tools/find_pattern.py <exe> "69 C0 90 01 00 00 8B 80 {d32}" [--limit 20]
"""
from __future__ import annotations

import argparse
import collections
import re
import struct
import sys

import pefile


def compile_pattern(text: str) -> tuple[re.Pattern[bytes], int]:
    parts = []
    captures = 0
    for token in text.split():
        if token == "??":
            parts.append(b".")
        elif token == "{d32}":
            parts.append(b"(....)")
            captures += 1
        else:
            parts.append(re.escape(bytes([int(token, 16)])))
    return re.compile(b"".join(parts), re.DOTALL), captures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe")
    parser.add_argument("pattern")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args(argv)

    pe = pefile.PE(args.exe, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    regex, captures = compile_pattern(args.pattern)
    histogram: collections.Counter[tuple[int, ...]] = collections.Counter()
    shown = 0
    for section in pe.sections:
        if not section.Characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            continue
        data = section.get_data()
        for match in regex.finditer(data):
            va = base + section.VirtualAddress + match.start()
            values = tuple(struct.unpack("<I", g)[0] for g in match.groups())
            histogram[values] += 1
            if shown < args.limit:
                print(f"{va:#010x} " + " ".join(f"{v:#010x}" for v in values))
                shown += 1
    total = sum(histogram.values())
    print(f"{total} match(es)")
    if captures:
        for values, count in histogram.most_common(10):
            print(f"  {count:5}x " + " ".join(f"{v:#010x}" for v in values))
    return 0 if total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
