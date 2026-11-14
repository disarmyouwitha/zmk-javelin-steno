"""Tests for upload.py against a fake keyboard that speaks the serial protocol.

No hardware and no pyserial needed. Run from anywhere:

    python3 tools/steno-squid-lapwing/test_upload.py

The fake keyboard answers like the firmware: it reports each image's expected
size and CRC, accepts the preflight and write passes in 512-byte chunks, and
refuses to erase anything the script did not ask it to. Time is faked, so the
silent-firmware cases do not wait. Every test also fails if the script ever
asks a question, because project scripts must never prompt.
"""

import builtins
import collections
import contextlib
import io
import sys
import tempfile
import types
import unittest
import zlib
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.modules.setdefault("serial", types.SimpleNamespace(Serial=object))
import upload  # noqa: E402

LETTERS = {
    "LAPWING": dict(zip(b"?LEVI", ["query", "program", "erase", "verify", "info"])),
    "PLOVER": dict(zip(b"pPevi", ["query", "program", "erase", "verify", "info"])),
    "EXTRAS": dict(zip(b"xXKkn", ["query", "program", "erase", "verify", "info"])),
}
DIAG_REPLIES = {
    b"S": ["BUILD abc", "[b1 2.0] noise", "FLASH mapped=1", "END", "after"],
    b"D": ["LOG BEGIN 10 bytes", "[b1 0.1] x", "LOG END"],
    b"Z": ["CLEARED log and counters"],
    b"R": ["RAIL enable level high", "RAIL END"],
    b"T": ["BENCH cycle 1 ok", "BENCH END"],
}


def make_image(seed, words=300):
    """A word-aligned JSC4 image of a few kilobytes, different per seed."""
    body = bytes((seed * 31 + i * 7) & 0xFF for i in range(words * 4 - 4))
    return b"JSC4" + body


class FakeTime:
    """Advances on every look at the clock, so waiting for a silent keyboard is instant."""

    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        self.now += 0.25
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Keyboard:
    def __init__(self, images, installed=(), damaged=()):
        self.images = {}
        for name, data in images.items():
            self.images[name] = {
                "data": data, "size": len(data), "crc": zlib.crc32(data),
                "state": "VERIFIED" if name in installed else "UNPROVISIONED",
                "damaged": name in damaged,
            }
        self.out = collections.deque()
        self.phase = None
        self.received = 0
        self.log = []
        self.sent = []

    def say(self, name, text, value):
        self.out.append(f"{name} {text} {value}\r\n".encode())

    def mapped(self):
        return all(i["state"] == "VERIFIED" for i in self.images.values())

    def write(self, data):
        self.sent.append(data)
        if self.phase:
            name, kind = self.phase
            image = self.images[name]
            assert len(data) <= upload.CHUNK_BYTES
            offset = self.received
            assert image["data"][offset:offset + len(data)] == data, "chunk differs from image"
            self.received += len(data)
            self.say(name, "ACK", self.received)
            if self.received == image["size"]:
                self.received = 0
                if kind == "PREFLIGHT":
                    self.phase = (name, "WRITE")
                    self.say(name, "WRITE", image["size"])
                else:
                    self.phase = None
                    image["state"] = "VERIFIED"
                    self.say(name, "MAPPED" if self.mapped() else "VERIFIED", image["size"])
            return
        assert len(data) == 1, data
        if data in DIAG_REPLIES:
            for line in DIAG_REPLIES[data]:
                self.out.append((line + "\r\n").encode())
            return
        for name, image in self.images.items():
            action = LETTERS[name].get(data[0])
            if not action:
                continue
            self.log.append((name, action))
            if action == "info":
                self.say(name, "SIZE", image["size"])
                self.say(name, "CRC", image["crc"])
            elif action == "query":
                self.say(name, "MAPPED" if self.mapped() else image["state"], image["size"])
            elif action == "program":
                assert image["state"] == "UNPROVISIONED", f"{name} was written over data"
                self.phase = (name, "PREFLIGHT")
                self.say(name, "PREFLIGHT", image["size"])
            elif action == "verify":
                if image["damaged"]:
                    self.say(name, "VERIFY_MISMATCH", image["crc"] ^ 1)
                else:
                    self.say(name, "VERIFY_OK", image["crc"])
            elif action == "erase":
                image["state"] = "UNPROVISIONED"
                image["damaged"] = False
                self.say(name, "ERASE", 256 * 1024)
                self.say(name, "ERASED", image["size"])

    def readline(self):
        return self.out.popleft() if self.out else b""

    def reset_input_buffer(self):
        self.out.clear()

    def flush(self):
        pass


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        root = Path(self.dir.name)
        self.images = {"LAPWING": make_image(1), "PLOVER": make_image(2), "EXTRAS": make_image(3)}
        self.paths = {}
        for name, data in self.images.items():
            self.paths[name] = root / f"{name.lower()}.bin"
            self.paths[name].write_bytes(data)

    def run_script(self, keyboard, *args):
        class FakeSerial:
            def __init__(self, *a, **k):
                pass

            def __enter__(self):
                return keyboard

            def __exit__(self, *a):
                return False

        def no_prompt(*a, **k):
            raise AssertionError("the script asked a question")

        out = io.StringIO()
        code = None
        with contextlib.redirect_stdout(out), \
                mock.patch.object(upload, "Serial", FakeSerial), \
                mock.patch.object(upload, "time", FakeTime()), \
                mock.patch.object(upload, "DEFAULT_IMAGES", self.paths), \
                mock.patch.object(builtins, "input", no_prompt), \
                mock.patch.object(sys, "argv", ["upload.py", "COM1", *args]):
            try:
                upload.main()
            except SystemExit as exit_:
                code = exit_.code
        return code, out.getvalue()

    def keyboard(self, names=("LAPWING", "PLOVER", "EXTRAS"), installed=(), damaged=()):
        return Keyboard({n: self.images[n] for n in names}, installed, damaged)

    def programmed(self, keyboard):
        return [n for n, a in keyboard.log if a == "program"]

    def test_fresh_keyboard_gets_every_image(self):
        kb = self.keyboard()
        code, _ = self.run_script(kb)
        self.assertIsNone(code)
        self.assertEqual(self.programmed(kb), ["LAPWING", "PLOVER", "EXTRAS"])
        self.assertTrue(kb.mapped())

    def test_installed_images_are_left_alone(self):
        kb = self.keyboard(installed=("LAPWING", "PLOVER", "EXTRAS"))
        code, text = self.run_script(kb)
        self.assertIsNone(code)
        self.assertEqual(self.programmed(kb), [])
        self.assertEqual(text.count("already installed"), 3)

    def test_only_the_missing_image_is_written(self):
        kb = self.keyboard(installed=("LAPWING", "PLOVER"))
        self.run_script(kb)
        self.assertEqual(self.programmed(kb), ["EXTRAS"])

    def test_firmware_without_extras_is_skipped(self):
        kb = self.keyboard(names=("LAPWING", "PLOVER"))
        code, text = self.run_script(kb)
        self.assertIsNone(code)
        self.assertIn("EXTRAS: not part of this firmware, skipped", text)
        self.assertEqual(self.programmed(kb), ["LAPWING", "PLOVER"])

    def test_wrong_image_file_is_refused_before_any_write(self):
        self.paths["PLOVER"].write_bytes(make_image(9))
        kb = self.keyboard()
        code, text = self.run_script(kb)
        self.assertIn("is not the PLOVER image this firmware was built for", str(code))
        self.assertNotIn("PLOVER", [n for n, a in kb.log if a == "program"])

    def test_status_reads_only(self):
        kb = self.keyboard(installed=("LAPWING",))
        code, text = self.run_script(kb, "--status")
        self.assertIsNone(code)
        self.assertIn("LAPWING: VERIFIED", text)
        self.assertIn("PLOVER: UNPROVISIONED", text)
        self.assertEqual([a for _, a in kb.log if a in ("program", "erase")], [])

    def test_verify_passes_and_writes_nothing(self):
        kb = self.keyboard(installed=("LAPWING", "PLOVER", "EXTRAS"))
        code, text = self.run_script(kb, "--verify")
        self.assertEqual(code, 0)
        self.assertEqual(text.count(": OK, CRC32"), 3)
        self.assertEqual([a for _, a in kb.log if a in ("program", "erase")], [])

    def test_verify_reports_a_damaged_image_with_exit_code_1(self):
        kb = self.keyboard(installed=("LAPWING", "PLOVER", "EXTRAS"), damaged=("PLOVER",))
        code, text = self.run_script(kb, "--verify")
        self.assertEqual(code, 1)
        self.assertIn("PLOVER: FAILED", text)

    def test_repair_erases_then_rewrites_one_base(self):
        kb = self.keyboard(installed=("LAPWING", "PLOVER", "EXTRAS"), damaged=("PLOVER",))
        code, _ = self.run_script(kb, "--repair", "--base", "plover")
        self.assertIsNone(code)
        self.assertEqual([(n, a) for n, a in kb.log if a in ("erase", "program")],
                         [("PLOVER", "erase"), ("PLOVER", "program")])
        self.assertEqual(kb.images["PLOVER"]["state"], "VERIFIED")

    def test_repair_with_wrong_image_erases_nothing(self):
        self.paths["PLOVER"].write_bytes(make_image(9))
        kb = self.keyboard(installed=("LAPWING", "PLOVER", "EXTRAS"))
        with self.assertRaises(RuntimeError) as caught:
            self.run_script(kb, "--repair", "--base", "plover")
        self.assertIn("Nothing was erased", str(caught.exception))
        self.assertEqual([a for _, a in kb.log if a == "erase"], [])

    def test_repair_needs_a_single_base(self):
        code, _ = self.run_script(self.keyboard(), "--repair")
        self.assertEqual(code, 2)

    def test_single_base_upload_by_image_path(self):
        kb = self.keyboard()
        code, _ = self.run_script(kb, "--base", "lapwing", str(self.paths["LAPWING"]))
        self.assertIsNone(code)
        self.assertEqual(self.programmed(kb), ["LAPWING"])

    def test_diag_commands_stop_at_their_end_line(self):
        for what, letter in (("status", b"S"), ("log", b"D"), ("clear", b"Z"),
                             ("rail", b"R"), ("bench", b"T")):
            with self.subTest(what=what):
                kb = self.keyboard()
                code, text = self.run_script(kb, "--diag", what)
                self.assertIsNone(code)
                self.assertEqual(kb.sent, [letter])
                self.assertIn(upload.DIAG[what][1], text)
        # S stops at END, so the line after it stays unread.
        kb = self.keyboard()
        _, text = self.run_script(kb, "--diag", "status")
        self.assertNotIn("after", text)

    def test_diag_without_a_reply_says_which_build_it_needs(self):
        class Silent(Keyboard):
            def write(self, data):
                self.sent.append(data)

        kb = Silent({})
        code, _ = self.run_script(kb, "--diag", "status")
        self.assertIn("CONFIG_ZMK_JAVELIN_DIAG", str(code))

    def test_diag_stands_alone(self):
        code, _ = self.run_script(self.keyboard(), "--diag", "status", "--verify")
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
