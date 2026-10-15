"""Port a global's address from one build to another by matching the code around its references.

For every reference to <address> in the source exe's code, the bytes before and after the 4-byte
operand are used as a pattern (operand wildcarded) in the target exe. The operand values found in
the target are counted; the most frequent value is the likely ported address.

Usage:
    python tools/port_address.py <source exe> <hex address> <target exe> [--before 6] [--after 6]
"""
from __future__ import annotations

import argparse
import collections
import re
import struct
import sys

import pefile


def code_sections(pe: pefile.PE):
    for section in pe.sections:
        if section.Characteristics & 0x20000000:
            yield section, section.get_data()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("address")
    parser.add_argument("target")
    parser.add_argument("--before", type=int, default=6)
    parser.add_argument("--after", type=int, default=6)
    args = parser.parse_args(argv)

    address = int(args.address, 16)
    needle = struct.pack("<I", address)
    source = pefile.PE(args.source, fast_load=True)
    target = pefile.PE(args.target, fast_load=True)
    target_code = [data for _, data in code_sections(target)]

    votes: collections.Counter[int] = collections.Counter()
    references = 0
    unique_hits = 0
    for _, data in code_sections(source):
        for match in re.finditer(re.escape(needle), data):
            references += 1
            start = match.start()
            before = data[max(0, start - args.before):start]
            after = data[start + 4:start + 4 + args.after]
            pattern = re.compile(re.escape(before) + b"(....)" + re.escape(after), re.DOTALL)
            found = [struct.unpack("<I", m.group(1))[0] for blob in target_code for m in pattern.finditer(blob)]
            if len(found) == 1:
                unique_hits += 1
            for value in set(found):
                votes[value] += 1
    print(f"{references} reference(s) in source, {unique_hits} with a unique match in target")
    for value, count in votes.most_common(8):
        print(f"  {count:4} vote(s): {value:#010x}")
    return 0 if votes else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
