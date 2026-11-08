"""Provision or check the Zen R4 dictionary over its USB serial interface."""

import argparse
from pathlib import Path
import time
import zlib

from serial import Serial


CHUNK_BYTES = 512


def response(port, timeout=30):
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
        if line.startswith("LAPWING "):
            _, state, value = line.split()
            if state.endswith("ERROR") or state == "FLASH_CONFLICT":
                raise RuntimeError(f"Device reported {state} at value {int(value):#x}")
            return state, int(value)
        print(line, flush=True)
    raise TimeoutError("No Lapwing reply on USB serial")


def query(port):
    # The initial ZMK boot log can fill the CDC transmit buffer before DTR.
    for _ in range(4):
        port.write(b"?")
        try:
            return response(port, 2)
        except TimeoutError:
            continue
    raise TimeoutError("The firmware did not answer a Lapwing status query")


def transfer(port, image, stage):
    for offset in range(0, len(image), CHUNK_BYTES):
        chunk = image[offset:offset + CHUNK_BYTES]
        port.write(chunk)
        end = offset + len(chunk)
        if response(port, 10) != ("ACK", end):
            raise RuntimeError(f"Unexpected acknowledgment for {stage} at {offset:#x}")
        if end == len(image) or end % (256 * 1024) == 0:
            print(f"{stage}: {end:,}/{len(image):,} bytes", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC device path")
    parser.add_argument("image", type=Path, nargs="?", help="Mapped JSC4 image to program")
    args = parser.parse_args()
    image = args.image.read_bytes() if args.image else None
    if image is not None and (image[:4] != b"JSC4" or len(image) % 4):
        parser.error("Expected a word-aligned JSC4 image")

    with Serial(args.port, 115200, timeout=0.25, write_timeout=5, exclusive=True) as port:
        state, size = query(port)
        if image is None:
            print(f"{state}: {size:,} bytes")
            return

        if size != len(image):
            raise RuntimeError(f"Firmware expects {size:,} bytes")
        if state == "MAPPED":
            print("Dictionary is already verified and mapped")
            return
        if state != "UNPROVISIONED":
            raise RuntimeError(f"Unexpected starting state {state}")

        print(f"Lapwing image: {len(image):,} bytes, CRC32 {zlib.crc32(image):08x}", flush=True)
        port.write(b"L")
        if response(port) != ("PREFLIGHT", size):
            raise RuntimeError("Device did not start the preflight")
        transfer(port, image, "Preflight")
        if response(port) != ("WRITE", size):
            raise RuntimeError("Device did not accept the preflight")
        transfer(port, image, "Program and read back")
        if response(port, 90) != ("MAPPED", size):
            raise RuntimeError("Device did not verify the mapped dictionary")
        print("Dictionary readback, mapped CRC, and native Javelin lookups passed")


if __name__ == "__main__":
    main()
