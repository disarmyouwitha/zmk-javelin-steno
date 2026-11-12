"""Provision, check, verify or repair the Zen R4 dictionaries over USB serial."""

import argparse
from pathlib import Path
import re
import sys
import time
import zlib

from serial import Serial


CHUNK_BYTES = 512
# Serial command letters understood by the firmware, by base.
COMMANDS = {
    "LAPWING": {"query": b"?", "program": b"L", "erase": b"E", "verify": b"V", "info": b"I"},
    "PLOVER": {"query": b"p", "program": b"P", "erase": b"e", "verify": b"v", "info": b"i"},
}


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
            if state == "FLASH_CONFLICT":
                raise RuntimeError(
                    f"The flash already holds different data at {int(value):#x}. "
                    "Run again with --repair to erase this base first.")
            if state.endswith("ERROR"):
                raise RuntimeError(f"Device reported {state} at value {int(value):#x}")
            return state, int(value)
        print(line, flush=True)
    raise TimeoutError("No dictionary reply on USB serial")


def ask(port, base, command, expected):
    """Send a command and return the value of its first reply, which must be `expected`."""
    # The initial ZMK boot log can fill the CDC transmit buffer before DTR.
    for _ in range(4):
        port.write(COMMANDS[base][command])
        try:
            state, value = response(port, 2, base)
        except TimeoutError:
            continue
        if expected is not None and state != expected:
            raise RuntimeError(f"Unexpected reply {state}")
        return state, value
    raise TimeoutError("The firmware did not answer. It may predate this command.")


def query(port, base="LAPWING"):
    return ask(port, base, "query", None)


def expected_image(port, base):
    """The size and CRC32 of the image this firmware was built for."""
    _, size = ask(port, base, "info", "SIZE")
    state, crc = response(port, 2, base)
    if state != "CRC":
        raise RuntimeError(f"Unexpected reply {state}")
    # A retried request leaves a second answer on its way. Drop it.
    time.sleep(0.5)
    port.reset_input_buffer()
    return size, crc


def transfer(port, image, stage, base="LAPWING"):
    for offset in range(0, len(image), CHUNK_BYTES):
        chunk = image[offset:offset + CHUNK_BYTES]
        port.write(chunk)
        end = offset + len(chunk)
        if response(port, 10, base) != ("ACK", end):
            raise RuntimeError(f"Unexpected acknowledgment for {stage} at {offset:#x}")
        if end == len(image) or end % (256 * 1024) == 0:
            print(f"{stage}: {end:,}/{len(image):,} bytes", flush=True)


def program(port, image, base):
    size = len(image)
    port.write(COMMANDS[base]["program"])
    if response(port, base=base) != ("PREFLIGHT", size):
        raise RuntimeError("Device did not start the preflight")
    transfer(port, image, "Preflight", base)
    if response(port, base=base) != ("WRITE", size):
        raise RuntimeError("Device did not accept the preflight")
    transfer(port, image, "Program and read back", base)
    if response(port, 90, base) not in (("MAPPED", size), ("VERIFIED", size)):
        raise RuntimeError("Device did not verify the mapped dictionary")
    print("Dictionary verified. Query status for MAPPED once both bases are provisioned.")


def verify(port, base):
    """Full CRC check of one base. Reads only. Returns True when it matches."""
    port.write(COMMANDS[base]["verify"])
    state, value = response(port, 120, base)
    if state == "VERIFY_OK":
        print(f"{base}: OK, CRC32 {value:08x}")
        return True
    if state == "VERIFY_MISMATCH":
        print(f"{base}: FAILED, the flash holds CRC32 {value:08x}, not the expected image")
    elif state == "VERIFY_READ_FAILED":
        print(f"{base}: FAILED, read error at offset {value:#x}")
    else:
        print(f"{base}: FAILED, unexpected reply {state}")
    return False


def repair(port, image, base):
    # Check the replacement before touching the flash.
    size, crc = expected_image(port, base)
    if len(image) != size or zlib.crc32(image) != crc:
        raise RuntimeError(
            f"This is not the image the firmware was built for. It expects {size:,} bytes "
            f"with CRC32 {crc:08x}; the file has {len(image):,} bytes with CRC32 "
            f"{zlib.crc32(image):08x}. Nothing was erased.")
    print(f"{base} image matches the firmware: {size:,} bytes, CRC32 {crc:08x}")
    print("Erasing", flush=True)
    port.write(COMMANDS[base]["erase"])
    while True:
        state, value = response(port, 15, base)
        if state == "ERASED":
            break
        if state != "ERASE":
            raise RuntimeError(f"Unexpected reply {state} during the erase")
        if value % (256 * 1024) == 0:
            print(f"Erase: {value:,} bytes", flush=True)
    program(port, image, base)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC device path")
    parser.add_argument("image", type=Path, nargs="?", help="Mapped JSC4 image to program")
    parser.add_argument("--base", choices=("lapwing", "plover", "both"), default="lapwing",
                        help="Base dictionary to act on. 'both' is for --verify only")
    parser.add_argument("--repair", action="store_true",
                        help="Erase this base and write the image again, even if the "
                             "keyboard reports it as verified or cannot load it")
    parser.add_argument("--verify", action="store_true",
                        help="Read the whole base and check its CRC. Writes nothing")
    args = parser.parse_args()
    if args.verify and (args.repair or args.image):
        parser.error("--verify takes no image and cannot be combined with --repair")
    if args.repair and not args.image:
        parser.error("--repair needs the image to write")
    if args.base == "both" and not args.verify:
        parser.error("--base both is for --verify only")
    base = args.base.upper()
    image = args.image.read_bytes() if args.image else None
    if image is not None and (image[:4] != b"JSC4" or len(image) % 4):
        parser.error("Expected a word-aligned JSC4 image")

    with Serial(args.port, 115200, timeout=0.25, write_timeout=5, exclusive=True) as port:
        if args.verify:
            bases = ["LAPWING", "PLOVER"] if base == "BOTH" else [base]
            # Wait out the boot log before the long read.
            expected_image(port, bases[0])
            results = [verify(port, name) for name in bases]
            sys.exit(0 if all(results) else 1)

        if args.repair:
            repair(port, image, base)
            return

        state, size = query(port, base)
        if image is None:
            print(f"{state}: {size:,} bytes")
            return

        if size != len(image):
            raise RuntimeError(f"Firmware expects {size:,} bytes")
        if state in ("MAPPED", "VERIFIED"):
            print("Dictionary is already verified. Use --repair to erase and write it again.")
            return
        if state != "UNPROVISIONED":
            raise RuntimeError(f"Unexpected starting state {state}")

        print(f"{base} image: {len(image):,} bytes, CRC32 {zlib.crc32(image):08x}", flush=True)
        program(port, image, base)


if __name__ == "__main__":
    main()
