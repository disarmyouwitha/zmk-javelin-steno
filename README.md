# ZMK + Javelin Steno

This ZMK module runs the [Javelin steno engine](https://github.com/jthlim/javelin-steno) on the keyboard. The tested full-dictionary build pairs a handwired Zen R4 left half with a Corne Ultralight right half. Both use nice!nano v2 controllers. The right half sends key positions to the left over Bluetooth, and the left connects to the computer over USB or Bluetooth. Normal typing, Plover HID, and onboard Javelin have all passed Bluetooth testing on a Mac with the left half powered by battery.

## What works on the tested split

| Mode | What happens |
| --- | --- |
| Normal typing | ZMK sends ordinary keyboard reports over USB or Bluetooth. |
| Onboard Javelin | The keyboard translates steno using the dictionaries in external flash and sends text over USB or Bluetooth. Host Plover can be closed. |
| Plover HID | The keyboard sends raw steno strokes to Plover on the computer over USB or Bluetooth. Plover uses its own dictionaries. |
| Javelin browser HID | Over USB, Javelin Steno Tools can edit and export the onboard personal dictionary, switch base dictionaries, and select a host output profile. Wireless browser editing is not implemented. |

From normal typing, the right thumbs are **Space**, **onboard Javelin**, and **Plover HID**, left to right. The middle left thumb holds **SYM+NUM**. In either steno mode, the outer left thumb returns to typing.

Both halves passed normal typing, cross-half Javelin chords, and host Plover HID, including split reconnection after a full reboot. Browser edits and base selection were also verified across a reboot. Onboard tests covered repeating a translation with the number bar, undoing the repeat with star, spelling **hoping** from `HOEP / -G`, and typing **ça va** on a Mac. Spelling and repeat run on the keyboard.

## Bluetooth to the computer

The battery-powered left half passed normal typing, Plover HID strokes, and onboard Javelin output including **ça va ça va** over Bluetooth on macOS. These tests used the existing firmware paths; no Bluetooth source changes were needed. Explicit repeat/undo and a full power-cycle/reconnect test over the host Bluetooth connection remain to be checked. Windows Bluetooth has not yet been verified.

From typing mode, hold the middle left thumb (**SYM+NUM**) and tap **Q** for host profile 0 or **W** for profile 1. Pair **Zen R4 Split** in the computer's Bluetooth settings. Disconnect USB to test Bluetooth output, since ZMK normally prefers USB when it is connected. The tested Mac pairing is saved to profile 0.

**SYM+NUM + R** clears the selected host profile's pairing. During bring-up, the Mac initially showed the keyboard as **SETTINGS RESET**, and pairing failed until an old bond in profile 0 was cleared. It then paired successfully and appeared as **Zen R4 Split**. Clearing a different empty profile did not resolve that stale bond. See the [Bluetooth test notes](docs/BLUETOOTH-BATTERY-TEST.md) for the observed sequence and remaining checks.

Battery operation is working, but battery life has not been measured. The current left configuration disables sleep and keeps the external dictionary flash powered.

## External QSPI flash

This project implements the W25Q128 flash integration on the left half, including QSPI setup and quad reads, serial provisioning with readback, and memory-mapped dictionary lookup from Javelin. The firmware checks each base image's CRC and verifies native lookups before enabling onboard translation. This puts both full base dictionaries in the 16 MiB external chip while the firmware fits in the nice!nano's internal flash.

| External flash contents | Outlines | Size |
| --- | ---: | ---: |
| Lapwing Base | 114,885 | 1,700,488 bytes |
| Plover Base | 147,424 | 2,178,864 bytes |
| Personal dictionary | Varies | 16 KiB of entry data, backed by two CRC-checked snapshots |

Lapwing is active by default. Enable **Plover Base** and disable **Lapwing Base** in the browser to use Plover's base for onboard translation. The keyboard remembers the selection. When enabled, personal entries take priority over both bases. If both bases are active, Lapwing wins on overlapping outlines.

## Edit dictionaries in the browser

1. Connect the left half over USB and open [Javelin Steno Tools](https://lim.au/#/software/javelin-steno-tools) in Chrome.
2. Choose **Connect via HID** and select **Zen R4 Split**.
3. Open **Active Dictionaries** to change the active bases, or **Dictionary Management → View** to add, replace, remove, or export personal entries.
4. Open **Host Layout** to select **Mac U.S.** or **Windows U.S. (plain text)**. The keyboard saves the selection. The Windows profile sends plain Latin letters for accented letters, such as c for ç and e for é, using the usual U.S. layout.

Mac accent output passed on hardware. The Windows profile uses ordinary ASCII keystrokes for consistent text entry across applications.

Javelin's browser HID interface is separate from Plover HID and currently requires USB. Plover HID also has a Bluetooth transport; it does not carry browser editor commands. Personal edits are saved to external flash. The base images are read-only from the browser and are provisioned over the diagnostics serial port.

## Build and wiring

- [Tested Zen R4 / Corne Ultralight split](config/zen-r4-split/README.md) has the build, flash, provisioning, pairing, and verification steps. [Split keymap](config/zen-r4-split/corne.keymap)
- [Standalone handwired left half](config/zen-r4-left/README.md) covers the 21-switch matrix, pin assignments, and Lapwing-only bring-up. [Wiring diagram](config/zen-r4-left/zen_r4_left_wiring_diagram.png)
- [QSPI diagnostics](tools/zen-r4-qspi/README.md) covers JEDEC detection, quad reads, flash readback, and the memory-mapped lookup bench.

Use the [pinned west manifest](config/west.yml). The JSC4 dictionary format depends on the pinned engine's native structure layout.

The earlier [Corne test profile](docs/CORNE-PLOVER-HID-TEST.md) is a separate target that runs a four-entry Javelin fixture from internal flash. The [`&javsteno` key bindings](include/dt-bindings/zmk/javelin_steno.h) apply to both builds.

## License

The Javelin steno engine is licensed under [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).
