"""Check every source outline using the pinned compact-map lookup algorithm."""

import argparse
import json
from pathlib import Path
import struct
import zlib

from compile_dict import parse_outline


def verify(source, blob, base):
    def word(offset):
        return struct.unpack_from("<I", blob, offset)[0]

    def pointer(offset):
        value = word(offset) - base
        assert 0 <= value < len(blob), f"Pointer outside image at {offset:#x}"
        return value

    assert len(blob) % 4 == 0
    assert blob[:4] == b"JSC4" and word(4) == 1
    assert word(32) == word(pointer(12) + word(8)), "Timestamp mismatch"
    definition = pointer(36)
    maximum = blob[definition + 1]
    assert 0 < maximum <= 16
    text = pointer(definition + 8)
    table = pointer(definition + 12)

    def lookup(outline):
        strokes = parse_outline(outline)
        length = len(strokes)
        assert 0 < length <= maximum
        entry = table + 12 * (length - 1)
        mask = word(entry)
        assert mask != 0
        data, blocks = pointer(entry + 4), pointer(entry + 8)
        index = zlib.crc32(b"".join(struct.pack("<I", stroke) for stroke in strokes)) & mask
        for _ in range(mask + 1):
            block, bit = blocks + 20 * (index // 128), index % 128
            part, shift = bit // 32, bit % 32
            bits = word(block + 4 * part)
            if not bits & (1 << shift):
                return None
            rank = (((bits << (31 - shift)) & 0xFFFFFFFF).bit_count() + word(block + 16)
                    + sum(word(block + 4 * j).bit_count() for j in range(part))) & 0xFFFFFFFF
            offset = data + rank * (3 + 3 * length)
            assert data <= offset and offset + 3 + 3 * length <= blocks
            found = tuple(int.from_bytes(blob[offset + 3 + 3 * j:offset + 6 + 3 * j], "little")
                          for j in range(length))
            if found == strokes:
                start = text + int.from_bytes(blob[offset:offset + 3], "little")
                return blob[start:blob.index(0, start)].decode()
            index = (index + 1) & mask
        return None

    for outline, expected in source.items():
        actual = lookup(outline)
        assert actual == expected, (outline, expected, actual)
    return len(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("--base-addr", type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()
    count = verify(json.loads(args.source.read_text()), args.image.read_bytes(), args.base_addr)
    print(f"PASS: {count:,} outlines match the mapped binary")


if __name__ == "__main__":
    main()
