# ZMK + Javelin Steno

This ZMK module runs the [Javelin steno engine](https://github.com/jthlim/javelin-steno) on the keyboard. The tested full-dictionary build pairs a handwired Zen R4 left half with a Corne Ultralight right half. Both use nice!nano v2 controllers. The right half sends key positions to the left over Bluetooth, and the left connects to the computer over USB or Bluetooth. Normal typing, Plover HID, and onboard Javelin have all passed Bluetooth testing on a Mac with the left half powered by battery.

## What works on the tested split

| Mode | What happens |
| --- | --- |
| Normal typing | ZMK sends ordinary keyboard reports over USB or Bluetooth. |
| Onboard Javelin | The keyboard translates steno using the dictionaries in external flash and sends text over USB or Bluetooth. Host Plover can be closed. |
| Plover HID | The keyboard sends raw steno strokes to Plover on the computer over USB or Bluetooth. Plover uses its own dictionaries. |
| Javelin browser HID | Over USB or Bluetooth, Javelin Steno Tools can edit and export the onboard personal dictionary, switch active dictionaries, and select a host output profile. Wireless connection was verified in Chrome on macOS. |

From normal typing, the right thumbs are **Space**, **onboard Javelin**, and **Plover HID**, left to right. The middle left thumb holds **SYM+NUM**. In either steno mode, the outer left thumb returns to typing.

Both halves passed normal typing, cross-half Javelin chords, and host Plover HID, including split reconnection after a full reboot. Browser edits and base selection were also verified across a reboot. Onboard tests covered repeating a translation with the number bar, undoing the repeat with star, spelling **hoping** from `HOEP / -G`, and typing **ça va** on a Mac. Spelling and repeat run on the keyboard.

## Bluetooth to the computer

The battery-powered left half passed normal typing, Plover HID strokes, and onboard Javelin output including **ça va ça va** over Bluetooth on macOS. The same Bluetooth connection also supports the Javelin browser editor. Explicit repeat/undo and a full power-cycle/reconnect test over the host Bluetooth connection remain to be checked. Windows Bluetooth has not yet been verified.

From typing mode, hold the middle left thumb (**SYM+NUM**) and tap **Q** for host profile 0 or **W** for profile 1. Pair **Zen R4 Split** in the computer's Bluetooth settings. Disconnect USB to test Bluetooth output, since ZMK normally prefers USB when it is connected. The tested Mac pairing is saved to profile 0.

**SYM+NUM + R** clears the selected host profile's pairing. During bring-up, the Mac initially showed the keyboard as **SETTINGS RESET**, and pairing failed until an old bond in profile 0 was cleared. It then paired successfully and appeared as **Zen R4 Split**. Clearing a different empty profile did not resolve that stale bond. See the [Bluetooth test notes](docs/BLUETOOTH-BATTERY-TEST.md) for the observed sequence and remaining checks.

## Deep sleep on battery

The left half enters deep sleep after 15 minutes without key activity while USB power is disconnected. It lets any in-progress dictionary save finish, stops QSPI, puts its signal pins in a high-impedance state, and switches off the flash rail and breakout LED. During ordinary Bluetooth idle, the flash stays available for the wireless dictionary editor.

Press a key on the left half to wake it. Startup restores flash power and checks the base dictionaries before onboard Javelin and the editor can use them. A cold boot reads both bases in full, which takes 20–40 seconds at the handwired half's 2 MHz flash clock. A wake from deep sleep checks the chip ID, the image headers and a few native lookups instead, since the flash was unpowered and unchanged while asleep. That quick wake path is new and still needs a hardware check. On the earlier firmware, the breakout LED went out in deep sleep and onboard Javelin translated successfully after a keypress wake. Powered USB insertion is also expected to wake the controller. [nRF52840 power specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html)

## External QSPI flash

This project implements the W25Q128 flash integration on the left half, including QSPI setup and quad reads, serial provisioning with readback, and memory-mapped dictionary lookup from Javelin. The firmware checks each base image's CRC and verifies native lookups before enabling onboard translation. This puts both full base dictionaries in the 16 MiB external chip while the firmware fits in the nice!nano's internal flash.

| External flash contents | Outlines | Size |
| --- | ---: | ---: |
| Lapwing Base | 114,885 | 1,700,488 bytes |
| Plover Base | 147,424 | 2,178,864 bytes |
| Personal dictionary | Varies | 16 KiB of entry data, backed by two CRC-checked snapshots |

Lapwing is active by default. Enable **Plover Base** and disable **Lapwing Base** in the browser to use Plover's base for onboard translation. The keyboard remembers the selection. When enabled, personal entries take priority over both bases. If both bases are active, Lapwing wins on overlapping outlines.

## Edit dictionaries in the browser

1. For wireless editing, pair **Zen R4 Split** in the Mac's Bluetooth settings and unplug the left half's USB cable. You can also connect the left half over USB for wired editing.
2. Open the existing [Javelin Steno Tools](https://lim.au/#/software/javelin-steno-tools) in Chrome. Choose **Connect via HID** for either connection and select **Zen R4 Split**.
3. Open **Active Dictionaries** to change the enabled dictionaries, or **Dictionary Management → View** to add, replace, remove, or export personal entries.
4. Open **Host Layout** to select **Mac U.S.** or **Windows U.S. (plain text)**. The keyboard saves the selection. The Windows profile sends plain Latin letters for accented letters, such as c for ç and e for é, using the usual U.S. layout.

Mac accent output passed on hardware. The Windows profile uses ordinary ASCII keystrokes for consistent text entry across applications.

Personal edits are saved to external flash. The browser can enable or disable either base dictionary. The diagnostics serial port provisions the base images themselves. With every dictionary disabled, onboard Javelin types literal outlines such as `KAT`.

The existing website loaded **Active Dictionaries** with USB unplugged. A separate Bluetooth HID check verified adding, replacing, reading back, and removing a temporary personal entry. See the [wireless editor test notes](docs/JAVELIN-BLUETOOTH-HID.md).

## Build and wiring

- [Tested Zen R4 / Corne Ultralight split](config/zen-r4-split/README.md) has the build, flash, provisioning, pairing, and verification steps. [Split keymap](config/zen-r4-split/corne.keymap)
- [Standalone handwired left half](config/zen-r4-left/README.md) covers the 21-switch matrix, pin assignments, and Lapwing-only bring-up. [Wiring diagram](config/zen-r4-left/zen_r4_left_wiring_diagram.png)
- [QSPI diagnostics](tools/zen-r4-qspi/README.md) covers JEDEC detection, quad reads, flash readback, and the memory-mapped lookup bench.

Use the [pinned west manifest](config/west.yml). The JSC4 dictionary format depends on the pinned engine's native structure layout.

The earlier [Corne test profile](docs/CORNE-PLOVER-HID-TEST.md) is a separate target that runs a four-entry Javelin fixture from internal flash. The [`&javsteno` key bindings](include/dt-bindings/zmk/javelin_steno.h) apply to both builds.

## License

The Javelin steno engine is licensed under [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).

## Handoff notes for Codex (branch `claude/wake-speed-and-cleanup`)

These changes came from a code review by Claude on 2026-11-10. **None of them have been compiled or run on hardware.** The review machine had no Zephyr toolchain. Build and test before merging to `main`.

### What changed

| Area | Change | Files |
| --- | --- | --- |
| Wake time | A wake from the keyboard's own deep sleep skips the two full CRC passes and does a quick check (JEDEC ID, JSC4 headers, KAT/hello native lookups). A cold boot still verifies in full. A failed quick check falls back to the full check. | `src/zen_r4/lapwing_flash.c` |
| Wake marker | On a successful suspend, the firmware writes a one-byte marker to `NRF_POWER->GPREGRET2`, derived from the expected image CRCs and sizes. It is read and cleared at boot. | `src/zen_r4/lapwing_flash.c` |
| Flash clock | New `CONFIG_ZMK_JAVELIN_STENO_QSPI_SCK_DIVIDER` (1–16, default 16, which is the previous 2 MHz). | `Kconfig`, `src/zen_r4/lapwing_flash.c` |
| Serial provisioning | Now optional: `CONFIG_ZMK_JAVELIN_STENO_SERIAL_PROVISIONING`, default on when `ZMK_USB_LOGGING` is on. `ZMK_JAVELIN_STENO_EXTERNAL_LAPWING` no longer depends on `ZMK_USB_LOGGING`, so the everyday build can omit `-S zmk-usb-logging`. | `Kconfig`, `src/zen_r4/lapwing_flash.c` |
| Flash worker | Without serial provisioning, the worker blocks on a semaphore instead of polling every 10 ms. | `src/zen_r4/lapwing_flash.c` |
| Engine patching | The build copies the Javelin engine into the build directory and patches the copy, so the pinned west checkout stays clean. | `CMakeLists.txt` |
| Logging | Per-key and per-HID-event `printk` calls are now `LOG_DBG`. | `src/engine_init.cc`, `src/platform/zmk_key_shim.cc` |
| Editor replies | `zen_javelin_console_line` returns `bool`. The browser gets `ERR Dictionary flash is not ready` when the engine cannot take a command, and a failed add reports that the personal dictionary may be full. | `src/engine_init.cc`, `src/zen_r4/javelin_web_hid.cc`, `src/zen_r4/user_dictionary_store.cc` |
| Display sync | Status snapshots retry for about 34 seconds after connect (4 at 1 s, then 6 at 5 s). | `src/zen_r4/split_status_central.c` |
| Dictionary compiler | Warns on stderr about translations of 128 UTF-8 bytes or more. The longest today is 58 bytes. | `tools/dict_compiler/compile_dict.py` |
| Right half | New conf file enables deep sleep. | `config/zen-r4-split/corne_right.conf` |

### What to verify

1. The left half builds both with and without `-S zmk-usb-logging`. The build without the snippet has never been compiled.
2. After deep sleep, a keypress wake reaches working onboard Javelin in a few seconds, and the log shows `External flash mapped after quick check`.
3. A cold boot (battery removed) still logs `after full check`.
4. `upload.py` still provisions and its query still reports the images as mapped, on a build with the snippet.
5. The right half sleeps, wakes and reconnects.
6. `GPREGRET2` is assumed to be untouched by the Adafruit bootloader and ZMK. If something clears it, wakes stay slow but remain correct.

### Decisions already made by the owner

- Do not add CI to this repository.
- Leave the right half's matrix and chip-select configuration alone until the first custom PCB is ordered.
- Base-dictionary provisioning stays on serial. No HID upload.
- Base dictionaries stay read-only; changes go in the personal dictionary.
- Dictionary saves keep blocking lookups, since both use the same flash.
