"""Install, check, verify or repair the dictionary images over USB serial.

With no options it installs every image the firmware knows that is not
installed yet, from the files under build/. Name one image with --base to
act on it alone; --repair and --verify are the usual reasons to.
"""

import argparse
from pathlib import Path
import re
import sys
import time
import zlib

from serial import Serial


CHUNK_BYTES = 512
REPO = Path(__file__).resolve().parents[2]
# Where the prepare scripts and the Lapwing build instructions write each image.
DEFAULT_IMAGES = {
    "LAPWING": REPO / "build" / "lapwing" / "lapwing-base.bin",
    "PLOVER": REPO / "build" / "plover" / "plover-main.bin",
    "EXTRAS": REPO / "build" / "extras" / "lapwing-extras.bin",
}
# Serial command letters understood by the firmware, by base.
COMMANDS = {
    "LAPWING": {"query": b"?", "program": b"L", "erase": b"E", "verify": b"V", "info": b"I"},
    "PLOVER": {"query": b"p", "program": b"P", "erase": b"e", "verify": b"v", "info": b"i"},
    # One image holding the Lapwing commands, numbers, UK additions and proper nouns.
    "EXTRAS": {"query": b"x", "program": b"X", "erase": b"K", "verify": b"k", "info": b"n"},
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


def ask(port, base, command, expected, drain=True):
    """Send a command and return its first reply, which must be `expected`."""
    # The initial ZMK boot log can fill the CDC transmit buffer before DTR.
    for attempt in range(4):
        port.write(COMMANDS[base][command])
        try:
            state, value = response(port, 2, base)
        except TimeoutError:
            continue
        if expected is not None and state != expected:
            raise RuntimeError(f"Unexpected reply {state}")
        if drain and attempt:
            # An earlier attempt may still be answered. Drop its reply, or the
            # next command would read it as its own.
            time.sleep(0.5)
            port.reset_input_buffer()
        return state, value
    raise TimeoutError("The firmware did not answer. It may predate this command.")


def query(port, base="LAPWING"):
    return ask(port, base, "query", None)


def expected_image(port, base):
    """The size and CRC32 of the image this firmware was built for."""
    _, size = ask(port, base, "info", "SIZE", drain=False)
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
    print("Dictionary verified. Query status for MAPPED once every image is provisioned.")


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


def read_image(path):
    """A JSC4 image file, or an error that says how to build it."""
    if not path.is_file():
        raise SystemExit(f"{path} not found. Build it with the matching prepare script first.")
    image = path.read_bytes()
    if image[:4] != b"JSC4" or len(image) % 4:
        raise SystemExit(f"{path} is not a word-aligned JSC4 image")
    return image


def present(port, name):
    """The size and CRC32 the firmware expects for `name`, or None if this firmware has no such image."""
    try:
        return expected_image(port, name)
    except TimeoutError:
        return None


def check_matches_firmware(image, name, size, crc, path):
    if len(image) != size or zlib.crc32(image) != crc:
        raise SystemExit(
            f"{path} is not the {name} image this firmware was built for. It expects "
            f"{size:,} bytes with CRC32 {crc:08x}; the file has {len(image):,} bytes with "
            f"CRC32 {zlib.crc32(image):08x}. Rebuild the image, or flash the firmware that "
            "was built with it.")


def upload_all(port, paths):
    """Install every image the firmware knows that is not installed yet."""
    seen = 0
    for name in COMMANDS:
        expected = present(port, name)
        if expected is None:
            print(f"{name}: not part of this firmware, skipped")
            continue
        seen += 1
        size, crc = expected
        state, _ = query(port, name)
        if state in ("MAPPED", "VERIFIED"):
            print(f"{name}: already installed")
            continue
        if state != "UNPROVISIONED":
            raise SystemExit(f"{name}: unexpected starting state {state}. "
                             f"Run with --repair --base {name.lower()} to erase and write it.")
        image = read_image(paths[name])
        check_matches_firmware(image, name, size, crc, paths[name])
        print(f"{name} image: {size:,} bytes, CRC32 {crc:08x}", flush=True)
        program(port, image, name)
    if not seen:
        raise SystemExit("The firmware reported no dictionary images")


def status(port):
    seen = 0
    for name in COMMANDS:
        if present(port, name) is None:
            continue
        seen += 1
        state, size = query(port, name)
        print(f"{name}: {state}, {size:,} bytes")
    if not seen:
        raise SystemExit("The firmware reported no dictionary images")


# Diagnostics commands in the logging build: letter sent, line that ends the reply.
# The third field is how long to wait for the reply, in seconds: the rail test
# takes about ten and the power-cycle bench about half a minute.
DIAG = {
    "status": (b"S", "END", 20),
    "log": (b"D", "LOG END", 20),
    "clear": (b"Z", "CLEARED", 20),
    "rail": (b"R", "RAIL END", 40),
    "bench": (b"T", "BENCH END", 90),
}


def diag(port, what):
    """Print the firmware's status, saved boot log, or clear both. Never prompts."""
    command, end, wait = DIAG[what]
    port.reset_input_buffer()
    port.write(command)
    port.flush()
    deadline = time.monotonic() + wait
    pending = b""
    while time.monotonic() < deadline:
        pending += port.readline()
        if not pending.endswith(b"\n"):
            continue
        line = pending.decode("utf-8", errors="replace").rstrip()
        pending = b""
        if line:
            print(line, flush=True)
        if line.startswith(end):
            return
    raise SystemExit("No diagnostics reply. This needs the logging build with "
                     "CONFIG_ZMK_JAVELIN_DIAG, which the default left-logging build has")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC device path")
    parser.add_argument("image", type=Path, nargs="?",
                        help="JSC4 image for a single --base. Default: the file under build/ "
                             "that the prepare script wrote")
    parser.add_argument("--base", choices=("lapwing", "plover", "extras", "all"),
                        help="Image to act on. With no --base, an upload installs every "
                             "image that is not installed yet and --verify checks all of "
                             "them. --repair needs one image")
    parser.add_argument("--repair", action="store_true",
                        help="Erase this image and write it again, even if the keyboard "
                             "reports it as verified or cannot load it")
    parser.add_argument("--verify", action="store_true",
                        help="Read the whole image and check its CRC. Writes nothing")
    parser.add_argument("--status", action="store_true",
                        help="Print the state of every image and exit")
    parser.add_argument("--diag", choices=sorted(DIAG),
                        help="status: counters, flash state, wake timing, power. log: the boot "
                             "log kept across the last reset. clear: wipe both. rail: switch "
                             "the flash rail both ways and time its rise and fall. bench: "
                             "power-cycle the flash ten times through the wake path")
    args = parser.parse_args()
    base = (args.base or "all").upper()
    if args.diag:
        if args.repair or args.verify or args.status or args.image:
            parser.error("--diag stands alone")
        with Serial(args.port, 115200, timeout=0.25, write_timeout=5, exclusive=True) as port:
            diag(port, args.diag)
        return
    if sum([args.repair, args.verify, args.status]) > 1:
        parser.error("Use only one of --repair, --verify and --status")
    if args.repair and base == "ALL":
        parser.error("--repair needs --base lapwing, plover or extras")
    if args.image and base == "ALL":
        parser.error("An image path goes with a single --base")
    if args.image and (args.verify or args.status):
        parser.error("--verify and --status take no image")

    paths = dict(DEFAULT_IMAGES)
    if args.image:
        paths[base] = args.image

    with Serial(args.port, 115200, timeout=0.25, write_timeout=5, exclusive=True) as port:
        if args.status:
            status(port)
            return

        if args.verify:
            bases = list(COMMANDS) if base == "ALL" else [base]
            # Wait out the boot log before the long read.
            expected_image(port, bases[0])
            results = []
            for name in bases:
                if len(bases) > 1 and name != bases[0] and present(port, name) is None:
                    # A firmware built without this image stays silent.
                    print(f"{name}: not part of this firmware, skipped")
                    continue
                results.append(verify(port, name))
            sys.exit(0 if all(results) else 1)

        if args.repair:
            repair(port, read_image(paths[base]), base)
            return

        if base == "ALL":
            upload_all(port, paths)
            return

        state, size = query(port, base)
        image = read_image(paths[base])
        if size != len(image):
            raise SystemExit(f"Firmware expects {size:,} bytes")
        if state in ("MAPPED", "VERIFIED"):
            print("Dictionary is already verified. Use --repair to erase and write it again.")
            return
        if state != "UNPROVISIONED":
            raise SystemExit(f"Unexpected starting state {state}")

        print(f"{base} image: {len(image):,} bytes, CRC32 {zlib.crc32(image):08x}", flush=True)
        program(port, image, base)


if __name__ == "__main__":
    main()
