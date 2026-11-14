"""Fetch the smaller pinned Lapwing dictionaries and compile them into one QSPI image.

The image is a single JSC4 collection holding four dictionaries. The firmware
shows each as its own checkbox. The order here must match extras_names in
src/steno_squid/lapwing_dictionary.cc.
"""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
from urllib.request import urlopen

COMMIT = "4ac5d53cf039a2b1f4c484456663c85e7df4e859"
URL = (f"https://raw.githubusercontent.com/aerickt/plover-lapwing-aio/{COMMIT}"
       "/plover_lapwing/dictionaries/")
# File, name shown in the browser, SHA-256 of the pinned source.
DICTIONARIES = [
    ("lapwing-commands.json", "Lapwing Commands",
     "6160e19084c77381f66aeda72473d807bcf3ea7f0c6303a37189ca60bf59ff3d"),
    ("lapwing-numbers.json", "Lapwing Numbers",
     "d104b9d6f8ea7935bdf3129d69bcdc4100e2b55fa717b41be2307043a4bd4b73"),
    ("lapwing-uk-additions.json", "Lapwing UK Additions",
     "e6d3190d0417c9912b34ebf9a3d02be08935d5c1c1b46bb47f0b580ed21717d3"),
    ("lapwing-proper-nouns.json", "Lapwing Proper Nouns",
     "aef0205fa4bc6bcb1bc11c199b0c01737ada5ffa7a0f0a56d48154d024ce66e8"),
]
# XIP base 0x12000000 plus EXTRAS_FLASH_OFFSET in src/steno_squid/lapwing_flash.h.
BASE_ADDRESS = "0x12500000"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("build/extras"))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    sources = []
    for file, _, sha256 in DICTIONARIES:
        with urlopen(URL + file, timeout=60) as response:
            source = response.read()
        if hashlib.sha256(source).hexdigest() != sha256:
            raise RuntimeError(f"Pinned {file} checksum mismatch")
        path = args.output_dir / file
        path.write_bytes(source)
        sources.append(path)

    image_path = args.output_dir / "lapwing-extras.bin"
    compiler = Path(__file__).resolve().parents[1] / "dict_compiler"
    command = [sys.executable, str(compiler / "compile_dict.py")] + [str(path) for path in sources]
    for _, name, _ in DICTIONARIES:
        command += ["-n", name]
    command += ["--base-addr", BASE_ADDRESS, "-o", str(image_path)]
    subprocess.run(command, check=True)
    for index, path in enumerate(sources):
        subprocess.run([sys.executable, str(compiler / "verify_mapped_dict.py"), str(path),
                        str(image_path), "--base-addr", BASE_ADDRESS, "--index", str(index),
                        "--count", str(len(sources))], check=True)


if __name__ == "__main__":
    main()
