#!/usr/bin/env python3
"""Export the Steno Squid's onboard battery log over USB CDC as CSV and JSON."""

import argparse
import csv
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import re
import time
import zlib

from serial import Serial
from serial.tools import list_ports


FIELDS = (
    "sequence", "boot", "uptime_ms", "battery_mv", "battery_pct", "event",
    "activity", "usb", "output", "layer", "flags", "left_presses", "right_presses",
    "usb_editor_commands", "ble_editor_commands", "reset_cause",
)
EVENTS = ("boot", "periodic", "usb_change", "sleep_prepare")
STATES = ("active", "idle", "sleep")
USB = ("unplugged", "powered", "hid")
OUTPUT = ("none", "usb", "ble")


def read_export(port):
    rows = []
    header = None
    pending = bytearray()
    attempts = 0
    next_request = 0
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if header is None and time.monotonic() >= next_request and attempts < 5:
            port.write(b"B")
            attempts += 1
            next_request = time.monotonic() + 3
        pending.extend(port.readline())
        if not pending.endswith(b"\n"):
            continue
        line = bytes(pending).rstrip(b"\r\n")
        pending.clear()
        if match := re.search(rb"ZBL BEGIN (\d+) (\d+) (\d+) (\d+)$", line):
            version, boot, uptime_ms, interval = map(int, match.groups())
            if version != 1:
                raise RuntimeError(f"Unsupported log format version {version}")
            header = (boot, uptime_ms, interval, datetime.now(timezone.utc))
            rows.clear()
        elif match := re.search(rb"ZBL ERROR (.*)$", line):
            raise RuntimeError(f"Keyboard battery log: {match[1].decode(errors='replace')}")
        elif header and (match := re.search(rb"ZBL ROW ([0-9a-f]{8}) ([-\d,]+)$", line)):
            payload = match[2]
            if zlib.crc32(payload) != int(match[1], 16):
                continue
            values = tuple(map(int, payload.split(b",")))
            if len(values) == len(FIELDS):
                rows.append(dict(zip(FIELDS, values)))
        elif header and (match := re.search(rb"ZBL END (\d+) (-?\d+)$", line)):
            count, status = map(int, match.groups())
            if status == -128 and count == 0 and attempts < 5:
                # The nRF52 CDC port received B before macOS finished asserting DTR.
                header = None
                continue
            if status:
                raise RuntimeError(f"Keyboard stopped the export with error {status}")
            if count != len(rows) or any(
                second["sequence"] != first["sequence"] + 1
                for first, second in zip(rows, rows[1:])
            ):
                raise RuntimeError("Incomplete export. Retry with the keyboard connected by USB.")
            return header, rows
    raise TimeoutError("No complete battery log response. Check the USB port and logging firmware.")


def decode_rows(header, rows):
    current_boot, device_uptime_ms, _, received_at = header
    boot_time = received_at - timedelta(milliseconds=device_uptime_ms)
    for row in rows:
        result = dict(row)
        for field, labels in (("event", EVENTS), ("activity", STATES),
                              ("usb", USB), ("output", OUTPUT)):
            result[field] = labels[row[field]]
        for field in ("battery_mv", "battery_pct"):
            if result[field] < 0:
                result[field] = ""
        flags = result.pop("flags")
        result["host_ble_connected"] = int(bool(flags & 1))
        result["flash_powered"] = int(bool(flags & 2))
        result["qspi_mapped"] = int(bool(flags & 4))
        result["split_ble_connected"] = int(bool(flags & 8))
        result["estimated_utc"] = (
            (boot_time + timedelta(milliseconds=row["uptime_ms"])).isoformat()
            if row["boot"] == current_boot else ""
        )
        yield result


def find_port():
    matches = [port.device for port in list_ports.comports() if port.description == "Steno Squid"]
    if len(matches) != 1:
        raise RuntimeError("Connect one Steno Squid over USB or specify --port.")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", type=Path, help="New output CSV file")
    parser.add_argument("--port", help="USB CDC port. Auto-detect by default.")
    parser.add_argument("--label", help="Experiment label. Defaults to the CSV filename stem.")
    parser.add_argument("--battery-mah", type=int, help="Nominal left battery capacity")
    parser.add_argument("--notes", help="Cell condition, LED state, firmware variant, or test notes")
    args = parser.parse_args()
    metadata_path = args.csv_path.with_suffix(".json")
    if args.csv_path.suffix.lower() != ".csv":
        parser.error("Output path must end in .csv")
    if args.csv_path.exists() or metadata_path.exists():
        parser.error("Choose a new filename to preserve prior exports")
    if args.battery_mah is not None and args.battery_mah <= 0:
        parser.error("Battery capacity must be positive")

    with Serial(args.port or find_port(), 115200, timeout=0.25, write_timeout=5,
                exclusive=True) as port:
        port.dtr = False
        time.sleep(0.25)
        port.dtr = True
        time.sleep(1)
        header, raw_rows = read_export(port)

    rows = list(decode_rows(header, raw_rows))
    fields = [field for field in FIELDS if field != "flags"] + [
        "host_ble_connected", "flash_powered", "qspi_mapped", "split_ble_connected",
        "estimated_utc",
    ]
    with args.csv_path.open("x", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    boot, uptime_ms, interval, received_at = header
    metadata = {
        "schema_version": 1,
        "label": args.label or args.csv_path.stem,
        "battery_mah": args.battery_mah,
        "notes": args.notes,
        "sample_interval_s": interval,
        "exported_at_utc": received_at.isoformat(),
        "boot_at_export": boot,
        "uptime_ms_at_export": uptime_ms,
        "rows": len(rows),
        "clock": "Uptime resets in deep sleep. Only the current boot has an estimated UTC anchor.",
    }
    with metadata_path.open("x") as output:
        json.dump(metadata, output, indent=2)
        output.write("\n")
    print(f"Saved {len(rows)} samples to {args.csv_path} and metadata to {metadata_path}")


if __name__ == "__main__":
    main()
