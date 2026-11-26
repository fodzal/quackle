"""Read and write Quackle's binary superleaves file, and enumerate a lexicon's leaves.

A record is the leave's length, its letters as Quackle letter codes in
ascending order, then (value + 128) * 256 as a little-endian uint16.
Letters are coded from QUACKLE_FIRST_LETTER (5) in the order of the alphabet
file, and the blank as QUACKLE_BLANK_MARK (1); in text a blank is '?'.
"""

import struct
from pathlib import Path

FIRST_LETTER = 5
BLANK = 1
BLANK_TEXT = "?"


class Alphabet:
    def __init__(self, path):
        self.code = {}
        self.text = {}
        self.count = {}
        next_code = FIRST_LETTER
        for line in Path(path).read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if not fields or fields[0].startswith("#"):
                continue
            if fields[0].lower().startswith("blank"):
                self.code[BLANK_TEXT] = BLANK
                self.text[BLANK] = BLANK_TEXT
                self.count[BLANK] = int(fields[2])
                continue
            self.code[fields[0]] = next_code
            self.text[next_code] = fields[0]
            self.count[next_code] = int(fields[3])
            next_code += 1

    def key(self, leave_text):
        """Leave text, as the harness prints it, to the sorted code tuple the engine looks up."""
        return tuple(sorted(self.code[c] for c in leave_text))

    def leave_text(self, key):
        return "".join(self.text[c] for c in key)

    def all_leaves(self, max_size):
        """Every multiset of 1..max_size tiles the bag can hold, as sorted code tuples."""
        codes = sorted(self.count)
        out = []

        def extend(prefix, start):
            if prefix:
                out.append(tuple(prefix))
            if len(prefix) == max_size:
                return
            for i in range(start, len(codes)):
                c = codes[i]
                if prefix.count(c) < self.count[c]:
                    prefix.append(c)
                    extend(prefix, i)
                    prefix.pop()

        extend([], 0)
        return out


def read(path):
    data = Path(path).read_bytes()
    leaves = []
    i = 0
    while i < len(data):
        n = data[i]
        key = tuple(data[i + 1:i + 1 + n])
        (raw,) = struct.unpack_from("<H", data, i + 1 + n)
        leaves.append((key, raw / 256 - 128))
        i += 3 + n
    return leaves


def encode_value(value):
    return int((min(max(value, -128), 128 - 1 / 256) + 128) * 256)


def write(path, leaves):
    with open(path, "wb") as f:
        for key, value in leaves:
            f.write(bytes([len(key)]) + bytes(key) + struct.pack("<H", encode_value(value)))
