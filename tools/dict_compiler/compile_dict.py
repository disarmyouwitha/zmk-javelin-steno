#!/usr/bin/env python3
"""
Compile Plover JSON dictionaries into Javelin's JSC4 binary format.

Produces a StenoDictionaryCollection binary that can be embedded
in ZMK firmware at a known flash address.

Usage:
    python compile_dict.py main.json -o steno_dict.bin --base-addr 0
    python compile_dict.py main.json extra.json -o steno_dict.bin

For raw flash binaries, --base-addr must be the actual load address.
For embedded firmware use --asm-output: it emits linker relocations for each
native pointer. Plain .incbin does not relocate zero-based pointer values.
This compiler targets the pinned June 2026 engine's 32-bit JSC4 structure ABI.
"""

import argparse
import json
import struct
import sys
import zlib
from collections import defaultdict
from typing import Dict, List, Optional, Tuple


# --- Stroke parsing ---

STENO_KEYS = {'#': 1, 'S-': 2, 'T-': 4, 'K-': 8, 'P-': 16, 'W-': 32, 'H-': 64, 'R-': 128, 'A-': 256, 'O-': 512, '*': 1024, '-E': 2048, '-U': 4096, '-F': 8192, '-R': 16384, '-P': 32768, '-B': 65536, '-L': 131072, '-G': 262144, '-T': 524288, '-S': 1048576, '-D': 2097152, '-Z': 4194304}

IMPLICIT_HYPHEN = set('AOEU*')
LEFT_BANK = 'STKPWHR'
RIGHT_BANK = 'FRPBLGTSDZ'
VOWELS = 'AOEU'


# Match the pinned engine's EXTENDED_ENGLISH_STROKE_PARSER order, including
# numeric aliases. Digits assert the number bar and their physical key together.
STROKE_PARSER = [('#', 1), ('^', 0x800000), ('+', 0x1000000),
                ('1', 3), ('S', 2), ('2', 5), ('T', 4), ('K', 8),
                ('3', 17), ('P', 16), ('W', 32), ('4', 65), ('H', 64), ('R', 128),
                ('5', 257), ('A', 256), ('0', 513), ('O', 512), ('*', 1024), ('-', 0),
                ('E', 2048), ('U', 4096), ('6', 8193), ('F', 8192), ('R', 16384),
                ('7', 32769), ('P', 32768), ('B', 65536), ('8', 131073), ('L', 131072),
                ('G', 262144), ('9', 524289), ('T', 524288), ('S', 1048576),
                ('D', 2097152), ('Z', 4194304)]


def parse_stroke(s: str) -> int:
    result = 0
    cursor = 0
    for character, mask in STROKE_PARSER:
        if cursor < len(s) and s[cursor] == character:
            result |= mask
            cursor += 1
    if cursor != len(s) or not result:
        raise ValueError(f"Invalid or out-of-order steno stroke: {s!r}")
    return result


def parse_outline(outline: str) -> Tuple[int, ...]:
    return tuple(parse_stroke(s) for s in outline.split('/'))


# --- CRC32 (matching Javelin's implementation) ---

def crc32_hash(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def stroke_hash(strokes: Tuple[int, ...]) -> int:
    data = b''.join(struct.pack('<I', s) for s in strokes)
    return crc32_hash(data)


# --- Binary builder ---

def uint24(v: int) -> bytes:
    return struct.pack('<I', v)[:3]


def round_up(v: int, align: int) -> int:
    return (v + align - 1) & ~(align - 1)


def popcount32(v: int) -> int:
    return bin(v & 0xFFFFFFFF).count('1')


class CompactMapBuilder:
    """Build a compact map dictionary for a set of entries with the same stroke count."""

    def __init__(self, stroke_length: int, entries: Dict[Tuple[int, ...], int]):
        """entries maps stroke tuples to text block offsets."""
        self.stroke_length = stroke_length
        self.entries = entries

    def build(self) -> Tuple[bytes, bytes, int]:
        """Returns (data_block, hash_blocks, hashMapMask)."""
        if not self.entries:
            return b'', b'', 0

        # Determine hash map size: next power of 2 * 128, at least 2x entries
        n = len(self.entries)
        num_blocks = max(1, (n * 2 + 127) // 128)
        # Round up to power of 2
        p = 1
        while p < num_blocks:
            p *= 2
        num_blocks = p
        total_slots = num_blocks * 128
        hash_map_mask = total_slots - 1

        # Place entries using open addressing (linear probing)
        slots = [None] * total_slots
        for strokes, text_offset in self.entries.items():
            h = stroke_hash(strokes) & hash_map_mask
            while slots[h] is not None:
                h = (h + 1) & hash_map_mask
            slots[h] = (strokes, text_offset)

        # Build data block (entries in slot order) and hash map blocks.
        # Data entry = textOffset(3 bytes) + strokes(3 bytes each)
        entry_size = 3 + 3 * self.stroke_length
        data_block = bytearray()
        hash_blocks = bytearray()

        for block_idx in range(num_blocks):
            masks = [0, 0, 0, 0]
            block_entries = []

            for bit in range(128):
                slot_idx = block_idx * 128 + bit
                if slots[slot_idx] is not None:
                    masks[bit // 32] |= 1 << (bit % 32)
                    block_entries.append(slots[slot_idx])

            # Engine GetOffset uses inclusive rank within this block.
            # Subtract one so the first occupied slot has zero-based index.
            running_count = len(data_block) // entry_size
            base_offset = running_count - 1

            for m in masks:
                hash_blocks.extend(struct.pack('<I', m))
            hash_blocks.extend(struct.pack('<I', base_offset & 0xFFFFFFFF))

            for strokes, text_offset in block_entries:
                data_block.extend(uint24(text_offset))
                for s in strokes:
                    data_block.extend(uint24(s))

        return bytes(data_block), bytes(hash_blocks), hash_map_mask


class DictionaryCollectionBuilder:
    """Build a complete JSC4 StenoDictionaryCollection binary.

    Memory layout (all pointers are offsets from binary start):
      [StenoDictionaryCollection header]
      [StenoDictionaryDefinition pointers (one per dict)]
      [StenoCompactMapDictionaryDefinition structs]
      [StenoCompactMapDictionaryStrokesDefinition arrays]
      [Hash map blocks]
      [Data blocks]
      [Text block]
      [Timestamp (4 bytes)]
    """

    def __init__(self, base_addr: int = 0):
        self.base_addr = base_addr

    def build(self, dict_entries: List[Tuple[str, Dict[str, str]]]) -> bytes:
        # Parse all entries, group by stroke count per dictionary
        parsed_dicts = []
        for dict_name, raw_dict in dict_entries:
            by_length = defaultdict(dict)
            max_outline_len = 0
            for outline, translation in raw_dict.items():
                try:
                    strokes = parse_outline(outline)
                except (KeyError, ValueError) as error:
                    raise ValueError(f"{dict_name}: invalid outline {outline!r}") from error
                stroke_count = len(strokes)
                by_length[stroke_count][strokes] = translation
                max_outline_len = max(max_outline_len, stroke_count)
            parsed_dicts.append((dict_name, by_length, max_outline_len))

        # Build shared text block (deduplicated)
        all_texts = set()
        for _, by_length, _ in parsed_dicts:
            for entries in by_length.values():
                all_texts.update(entries.values())

        text_list = sorted(all_texts)
        text_to_offset = {}
        text_block = bytearray()
        for text in text_list:
            text_to_offset[text] = len(text_block)
            encoded = text.encode('utf-8')
            if len(encoded) >= 128:
                # The external-flash reader copies each translation through a
                # 128-byte buffer and drops anything longer.
                print(f"warning: {len(encoded)}-byte translation will not load from "
                      f"external flash: {text[:40]!r}...", file=sys.stderr)
            text_block.extend(encoded)
            text_block.append(0)

        # Replace translation strings with text offsets
        for _, by_length, _ in parsed_dicts:
            for stroke_len in by_length:
                entries = by_length[stroke_len]
                by_length[stroke_len] = {
                    strokes: text_to_offset[text]
                    for strokes, text in entries.items()
                }

        # Build compact map data for each (dict, stroke_length)
        # Each dict has a StrokesDefinition array indexed by stroke_length
        dict_stroke_data = []
        for dict_name, by_length, max_outline_len in parsed_dicts:
            stroke_defs = []
            for length in range(1, max_outline_len + 1):
                entries = by_length.get(length, {})
                builder = CompactMapBuilder(length, entries)
                data_block, hash_blocks, hash_map_mask = builder.build()
                stroke_defs.append((length, data_block, hash_blocks, hash_map_mask))
            dict_stroke_data.append((dict_name, stroke_defs, max_outline_len))

        # Now lay out the binary. We do two passes:
        # 1. Calculate sizes to determine offsets
        # 2. Write the actual data with correct pointers

        timestamp = 0x12345678

        # Header: StenoDictionaryCollection
        #   uint32_t magic
        #   uint16_t dictionaryCount
        #   bool hasReverseLookup
        #   uint8_t _padding
        #   SizedList<uint8_t> textBlock (count + ptr = 4 + ptr_size)
        #   SizedList<const uint8_t*> prefixes
        #   SizedList<const uint8_t*> suffixes
        #   uint32_t timestamp
        #   XipPointer<StenoDictionaryDefinition> dictionaries[]

        # On ARM32 (nRF52840): pointers are 4 bytes
        PTR = 4

        # SizedList<T> = { size_t count; T* data; } = 4 + PTR
        SIZED_LIST = 4 + PTR

        header_size = (
            4 +       # magic
            2 +       # dictionaryCount
            1 +       # hasReverseLookup
            1 +       # padding
            SIZED_LIST +  # textBlock
            SIZED_LIST +  # prefixes
            SIZED_LIST +  # suffixes
            4         # timestamp
        )
        dict_ptrs_size = len(parsed_dicts) * PTR

        # StenoCompactMapDictionaryDefinition:
        #   uint8_t defaultEnabled, maximumOutlineLength, type, options (= 4)
        #   XipPointer<char> name (= PTR)
        #   const uint8_t* textBlock (= PTR)
        #   const StrokesDefinition* strokes (= PTR)
        COMPACT_DEF_SIZE = 4 + PTR + PTR + PTR

        # StenoCompactMapDictionaryStrokesDefinition:
        #   size_t hashMapMask (= 4)
        #   const uint8_t* data (= PTR)
        #   const Block* offsets (= PTR)
        STROKES_DEF_SIZE = 4 + PTR + PTR

        # Layout plan
        offset = header_size + dict_ptrs_size

        # Align
        offset = round_up(offset, 4)

        # Dictionary definitions
        dict_def_offsets = []
        for i in range(len(parsed_dicts)):
            dict_def_offsets.append(offset)
            offset += COMPACT_DEF_SIZE

        offset = round_up(offset, 4)

        # Dictionary names (null-terminated strings)
        dict_name_offsets = []
        for dict_name, _, _ in dict_stroke_data:
            dict_name_offsets.append(offset)
            offset += len(dict_name.encode('utf-8')) + 1

        offset = round_up(offset, 4)

        # Strokes definitions arrays
        strokes_def_offsets = []  # [(dict_idx, [(length, offset)])]
        for di, (_, stroke_defs, max_len) in enumerate(dict_stroke_data):
            arr_offset = offset
            arr = []
            for si in range(len(stroke_defs)):
                arr.append(offset)
                offset += STROKES_DEF_SIZE
            strokes_def_offsets.append((arr_offset, arr))

        offset = round_up(offset, 4)

        # Data blocks (per dict, per stroke length)
        data_offsets = []  # [[(length, offset)]]
        for di, (_, stroke_defs, _) in enumerate(dict_stroke_data):
            doffs = []
            for si, (length, data_block, hash_blocks, _) in enumerate(stroke_defs):
                doffs.append(offset)
                offset += len(data_block)
            data_offsets.append(doffs)

        offset = round_up(offset, 4)

        # Hash map blocks (per dict, per stroke length)
        hash_offsets = []
        for di, (_, stroke_defs, _) in enumerate(dict_stroke_data):
            hoffs = []
            for si, (length, data_block, hash_blocks, _) in enumerate(stroke_defs):
                hoffs.append(offset)
                offset += len(hash_blocks)
            hash_offsets.append(hoffs)

        offset = round_up(offset, 4)

        # Text block
        text_block_offset = offset
        offset += len(text_block)

        # Timestamp at end of text block
        timestamp_end_offset = offset
        offset += 4

        total_size = offset

        # --- Pass 2: write binary ---

        buf = bytearray(total_size)
        base = self.base_addr
        self.pointer_offsets = []

        def write_u8(off, v):
            buf[off] = v & 0xFF

        def write_u16(off, v):
            struct.pack_into('<H', buf, off, v)

        def write_u32(off, v):
            struct.pack_into('<I', buf, off, v & 0xFFFFFFFF)

        def write_ptr(off, v):
            self.pointer_offsets.append(off)
            struct.pack_into('<I', buf, off, (base + v) & 0xFFFFFFFF)

        def write_bytes(off, data):
            buf[off:off+len(data)] = data

        def write_sized_list_u8(off, data_off, count):
            write_u32(off, count)
            write_ptr(off + 4, data_off)

        def write_sized_list_ptr(off, data_off, count):
            write_u32(off, count)
            write_ptr(off + 4, data_off)

        # Header
        p = 0
        write_u32(p, 0x3443534A); p += 4  # magic 'JSC4'
        write_u16(p, len(parsed_dicts)); p += 2  # dictionaryCount
        write_u8(p, 0); p += 1  # hasReverseLookup = false
        write_u8(p, 0); p += 1  # padding

        # textBlock SizedList
        write_sized_list_u8(p, text_block_offset, len(text_block)); p += SIZED_LIST

        # prefixes SizedList (empty)
        write_u32(p, 0); write_u32(p + 4, 0); p += SIZED_LIST

        # suffixes SizedList (empty)
        write_u32(p, 0); write_u32(p + 4, 0); p += SIZED_LIST

        # timestamp
        write_u32(p, timestamp); p += 4

        # Dictionary definition pointers
        for di in range(len(parsed_dicts)):
            write_ptr(p, dict_def_offsets[di]); p += PTR

        # Dictionary definitions
        for di, (_, stroke_defs, max_len) in enumerate(dict_stroke_data):
            off = dict_def_offsets[di]
            write_u8(off + 0, 1)  # defaultEnabled = true
            write_u8(off + 1, max_len)  # maximumOutlineLength
            write_u8(off + 2, 0)  # type = COMPACT_MAP
            write_u8(off + 3, 0)  # options
            write_ptr(off + 4, dict_name_offsets[di])  # name
            write_ptr(off + 4 + PTR, text_block_offset)  # textBlock
            # The constructor copies from the FIRST stored definition, then
            # subtracts one from its RAM cache pointer for length-based indexing.
            arr_start = strokes_def_offsets[di][0]
            write_ptr(off + 4 + PTR + PTR, arr_start)

        # Dictionary names
        for di, (dict_name, _, _) in enumerate(dict_stroke_data):
            write_bytes(dict_name_offsets[di], dict_name.encode('utf-8') + b'\x00')

        # Strokes definitions
        for di, (_, stroke_defs, _) in enumerate(dict_stroke_data):
            for si, (length, data_block, hash_blocks, hash_map_mask) in enumerate(stroke_defs):
                off = strokes_def_offsets[di][1][si]
                write_u32(off, hash_map_mask)
                write_ptr(off + 4, data_offsets[di][si])
                write_ptr(off + 4 + PTR, hash_offsets[di][si])

        # Data blocks
        for di, (_, stroke_defs, _) in enumerate(dict_stroke_data):
            for si, (length, data_block, hash_blocks, _) in enumerate(stroke_defs):
                write_bytes(data_offsets[di][si], data_block)

        # Hash map blocks
        for di, (_, stroke_defs, _) in enumerate(dict_stroke_data):
            for si, (length, data_block, hash_blocks, _) in enumerate(stroke_defs):
                write_bytes(hash_offsets[di][si], hash_blocks)

        # Text block
        write_bytes(text_block_offset, text_block)

        # Timestamp at end of text block
        write_u32(timestamp_end_offset, timestamp)

        # QSPI DMA and mapped CRC readers require a whole number of words.
        return bytes(buf) + bytes((-len(buf)) % 4)


def main():
    parser = argparse.ArgumentParser(
        description='Compile Plover JSON dictionaries to Javelin JSC4 binary')
    parser.add_argument('inputs', nargs='+', help='Input JSON dictionary files')
    parser.add_argument('-o', '--output', required=True, help='Output binary file')
    parser.add_argument('-n', '--name', action='append',
                        help='Dictionary name (one per input, defaults to filename)')
    parser.add_argument('--base-addr', type=lambda x: int(x, 0), default=0,
                        help='XIP base address for pointer resolution (default: 0)')
    parser.add_argument('--asm-output', help='Optional relocatable ARM assembly output')
    parser.add_argument('--max-entries', type=int, default=0,
                        help='Limit entries per dict (0 = no limit, for testing)')
    args = parser.parse_args()

    dict_entries = []
    for i, input_path in enumerate(args.inputs):
        with open(input_path, 'r') as f:
            raw = json.load(f)
        if args.max_entries > 0:
            items = list(raw.items())[:args.max_entries]
            raw = dict(items)
        name = args.name[i] if args.name and i < len(args.name) else input_path.rsplit('/', 1)[-1].rsplit('.', 1)[0]
        dict_entries.append((name, raw))
        print(f"Loaded {input_path}: {len(raw)} entries as '{name}'", file=sys.stderr)

    builder = DictionaryCollectionBuilder(base_addr=args.base_addr)
    result = builder.build(dict_entries)

    with open(args.output, 'wb') as f:
        f.write(result)

    if args.asm_output:
        if args.base_addr != 0:
            parser.error('--asm-output requires --base-addr 0')
        lines = ['.section .rodata.steno_dict,"a",%progbits', '.global _javelin_dict_start', '.global _javelin_dict_end', '.balign 4', '_javelin_dict_start:']
        cursor = 0
        for off in sorted(builder.pointer_offsets):
            chunk = result[cursor:off]
            for i in range(0, len(chunk), 16):
                lines.append('.byte ' + ','.join(str(v) for v in chunk[i:i+16]))
            target = struct.unpack_from('<I', result, off)[0]
            lines.append(f'.long _javelin_dict_start + {target}')
            cursor = off + 4
        chunk = result[cursor:]
        for i in range(0, len(chunk), 16):
            lines.append('.byte ' + ','.join(str(v) for v in chunk[i:i+16]))
        lines.append('_javelin_dict_end:')
        with open(args.asm_output, 'w') as f:
            f.write('\n'.join(lines) + '\n')

    print(f"Wrote {len(result)} bytes ({len(result)/1024:.1f} KB) to {args.output}", file=sys.stderr)


if __name__ == '__main__':
    main()
