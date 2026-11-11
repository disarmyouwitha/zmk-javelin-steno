"""Provision or check the Zen R4 dictionary over its USB serial interface."""

import argparse
from pathlib import Path
import re
import time
import zlib

from serial import Serial


CHUNK_BYTES = 512


def response(port, timeout=30, base="LAPWING"):
    deadline = time.monotonic() + timeout
    pending = b""
    while time.monotonic() < deadline:
        pending += port.readline()
        if not pending.endswith(b"\n"):
            continue
        line = pending.decode("utf-8", errors="replace").strip()
        pending = b""
        if not line:
            continue
        # Diagnostics can precede a reply on the same line.
        match = re.search(re.escape(base) + r" ([A-Z_]+) (\d+)", line)
        if match:
            state, value = match.groups()
            if state.endswith("ERROR") or state == "FLASH_CONFLICT":
                raise RuntimeError(f"Device reported {state} at value {int(value):#x}")
            return state, int(value)
        print(line, flush=True)
    raise TimeoutError("No dictionary reply on USB serial")


def query(port, base="LAPWING"):
    command = b"p" if base == "PLOVER" else b"?"
    # The initial ZMK boot log can fill the CDC transmit buffer before DTR.
    for _ in range(4):
        port.write(command)
        try:
            return response(port, 2, base)
        except TimeoutError:
            continue
    raise TimeoutError("The firmware did not answer a dictionary status query")


def transfer(port, image, stage, base="LAPWING"):
    for offset in range(0, len(image), CHUNK_BYTES):
        chunk = image[offset:offset + CHUNK_BYTES]
        port.write(chunk)
        end = offset + len(chunk)
        if response(port, 10, base) != ("ACK", end):
            raise RuntimeError(f"Unexpected acknowledgment for {stage} at {offset:#x}")
        if end == len(image) or end % (256 * 1024) == 0:
            print(f"{stage}: {end:,}/{len(image):,} bytes", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC device path")
    parser.add_argument("image", type=Path, nargs="?", help="Mapped JSC4 image to program")
    parser.add_argument("--base", choices=("lapwing", "plover"), default="lapwing")
    parser.add_argument("--force", action="store_true",
                        help="Erase and program again a base the firmware reports as verified")
    args = parser.parse_args()
    base = args.base.upper()
    image = args.image.read_bytes() if args.image else None
    if image is not None and (image[:4] != b"JSC4" or len(image) % 4):
        parser.error("Expected a word-aligned JSC4 image")

    with Serial(args.port, 115200, timeout=0.25, write_timeout=5, exclusive=True) as port:
        state, size = query(port, base)
        if image is None:
            print(f"{state}: {size:,} bytes")
            return

        if size != len(image):
            raise RuntimeError(f"Firmware expects {size:,} bytes")
        if state in ("MAPPED", "VERIFIED") and not args.force:
            print("Dictionary is already verified. Use --force to erase and program it again.")
            return
        if state not in ("MAPPED", "VERIFIED", "UNPROVISIONED"):
            raise RuntimeError(f"Unexpected starting state {state}")

        print(f"{base} image: {len(image):,} bytes, CRC32 {zlib.crc32(image):08x}", flush=True)
        # Erase first, so leftover or damaged contents cannot block the write.
        print("Erasing", flush=True)
        port.write(b"e" if base == "PLOVER" else b"E")
        while True:
            state, value = response(port, 10, base)
            if state == "ERASED":
                break
            if state != "ERASE":
                raise RuntimeError(f"Unexpected reply {state} during the erase")
        port.write(b"P" if base == "PLOVER" else b"L")
        if response(port, base=base) != ("PREFLIGHT", size):
            raise RuntimeError("Device did not start the preflight")
        transfer(port, image, "Preflight", base)
        if response(port, base=base) != ("WRITE", size):
            raise RuntimeError("Device did not accept the preflight")
        transfer(port, image, "Program and read back", base)
        if response(port, 90, base) not in (("MAPPED", size), ("VERIFIED", size)):
            raise RuntimeError("Device did not verify the mapped dictionary")
        print("Dictionary verified. Query status for MAPPED once both bases are provisioned.")


if __name__ == "__main__":
    main()
