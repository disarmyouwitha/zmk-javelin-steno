#!/usr/bin/env python3
"""Exercise the firmware hint reader against every one-stroke source entry.

Run after preparing the three dictionary images. Requires a C++17 compiler.
"""
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dictionary_compiler', ROOT / 'tools/dict_compiler/compile_dict.py')
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


def main():
    executable = ROOT / 'build/test-hint-index'
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(Path(__file__).with_suffix('.cc')), '-o', str(executable)], check=True)
    groups = [
        ('lapwing/lapwing-base', 0x12000000, ['lapwing/lapwing-base']),
        ('plover/plover-main', 0x12200000, ['plover/plover-main']),
        ('extras/lapwing-extras', 0x12500000, ['extras/lapwing-commands', 'extras/lapwing-numbers',
          'extras/lapwing-uk-additions', 'extras/lapwing-proper-nouns']),
    ]
    for image, base, sources in groups:
        queries = []
        for source in sources:
            best = {}
            for outline, text in json.loads((ROOT / f'build/{source}.json').read_text()).items():
                strokes = compiler.parse_outline(outline)
                if len(strokes) != 1 or not strokes[0]:
                    continue
                mask = strokes[0]
                old = best.get(text)
                if old is None or (mask.bit_count(), mask) < (old.bit_count(), old):
                    best[text] = mask
            for text, mask in best.items():
                # The line protocol skips control characters; firmware still
                # handles them as normal bytes in its bounded search.
                if '\n' not in text and '\r' not in text:
                    queries.append(f'{mask}\t{text}')
            queries += ['0\t__missing_hint__', '0\t', '--NEXT--']
        subprocess.run([str(executable), str(ROOT / f'build/{image}.bin'), hex(base)],
                       input='\n'.join(queries) + '\n', text=True, check=True)


if __name__ == '__main__':
    main()
