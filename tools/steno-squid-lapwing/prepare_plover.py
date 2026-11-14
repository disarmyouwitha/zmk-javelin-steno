"""Fetch and compile the pinned official Plover base into its own QSPI region."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
from urllib.request import urlopen

COMMIT = "8a52c5296c502507f9a46820a4a947ca25f998e6"
SOURCE_SHA256 = "6570873c7f703ecd39c9870083f643db3475b49462a8db80fe22fe33ac3cfe92"
URL = f"https://raw.githubusercontent.com/openstenoproject/plover/{COMMIT}/plover/assets/main.json"
BASE_ADDRESS = "0x12200000"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("build/plover"))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with urlopen(URL, timeout=60) as response:
        source = response.read()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise RuntimeError("Pinned Plover source checksum mismatch")
    source_path = args.output_dir / "plover-main.json"
    source_path.write_bytes(source)
    image_path = args.output_dir / "plover-main.bin"
    compiler = Path(__file__).resolve().parents[1] / "dict_compiler"
    subprocess.run([sys.executable, str(compiler / "compile_dict.py"), str(source_path),
                    "-n", "Plover Base", "--base-addr", BASE_ADDRESS, "-o", str(image_path)],
                   check=True)
    subprocess.run([sys.executable, str(compiler / "verify_mapped_dict.py"), str(source_path),
                    str(image_path), "--base-addr", BASE_ADDRESS], check=True)


if __name__ == "__main__":
    main()
