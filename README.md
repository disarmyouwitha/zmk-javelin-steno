# ZMK + Javelin Steno

This ZMK module runs the [Javelin steno engine](https://github.com/jthlim/javelin-steno) on the keyboard. The tested full-dictionary build pairs a handwired Zen R4 left half with a Corne Ultralight right half. Both use nice!nano v2 controllers. The right half sends key positions to the left over Bluetooth, and the left connects to the computer over USB or Bluetooth. Normal typing, Plover HID, and onboard Javelin have all passed Bluetooth testing on a Mac with the left half powered by battery.

## Validation of `claude/boot-speed-and-flash-idle`

Both local builds and all four GitHub Actions targets compiled. The user confirmed typing on both halves, Javelin KAT, Plover HID, mode labels, Practice and exit, idle flash shutdown, immediate Javelin wake, and all four browser-editor checks. The reboot diagnostic reported `CHECK_QUICK 53` (53 ms). Deep sleep with this branch and the new serial verify/repair commands still need hardware testing.

LEFT and RIGHT now have 30 prompts each. WORDS has 10. The longer runs and nine-character layer names passed host behavior and rendering checks and compiled for both halves. These follow-up changes still need flashing and on-keyboard testing.

**Get the code**

1. In this repository: `git fetch`, then `git checkout claude/boot-speed-and-flash-idle`.
2. Synchronize the west workspace's manifest with this checkout, then update its dependencies. The Mac workspace keeps a separate manifest copy. Set `repo` and `ws` to their absolute paths, then run:

   ```sh
   cp "$repo/config/west.yml" "$ws/config/west.yml"
   cd "$ws"
   west update
   west list zmk-hid-io-plover-hid -f '{url} {revision}'
   git -C zmk-hid-io-plover-hid rev-parse HEAD
   ```

   Expect the `disarmyouwitha/zmk-hid-io-plover-hid` fork at `25ca1c55e31781e49499178584520129b9586635`. Repeat this synchronization when switching to a checkout with a different manifest.

**Build and flash, without Studio first**

3. Build both halves with the commands in the [split guide](config/zen-r4-split/README.md#build). Or let GitHub build them: see [Automatic builds](#automatic-builds). Add `-p` to each `west build` for a clean rebuild, since the keymap, devicetree and build options changed. Keep `-S zmk-usb-logging` on the left for this round.
4. Confirm the local builds finish successfully and check their flash and RAM totals.
5. Flash **both** halves. The status messages between the halves changed, and an old right half ignores the new ones.

**Keyboard regression checklist**

6. First boot does one full dictionary check and records it. Reboot again: send `W` to the diagnostics port and expect `LAPWING CHECK_QUICK` with a time in milliseconds.
7. The steno layer shows **Loading...** on the right display until the check finishes, then **Javelin**.
8. Typing, onboard Javelin and Plover HID still work across both halves.
9. Practice: hold SYM+NUM and tap the outer right thumb. The key left of the Lapwing # key (the one above S) turns practice off and on. The outer left thumb returns to typing.
10. Browser editor: connect, edit an entry, then try a disconnect mid-command, a reconnect, and a switch between USB and Bluetooth.

**Then try ZMK Studio, on the left half only**

11. Rebuild the left with `-S studio-rpc-usb-uart` in place of `-S zmk-usb-logging`, and add `-DCONFIG_ZMK_STUDIO=y` after the `--`. Note the flash and RAM totals the build prints. If it does not fit, the build fails with the numbers.
12. Open ZMK Studio over USB. It starts unlocked. Work through the [Studio checks](#studio-checks-still-to-run-on-hardware).
13. Once Studio saves a keymap, the keyboard ignores later edits to the `.keymap` file until **Restore stock settings** is used in Studio.

**Optional**

14. Battery test: the dictionary flash now powers down after 30 idle seconds outside the steno layer. To compare against always-on, uncomment `CONFIG_ZMK_JAVELIN_STENO_FLASH_IDLE_OFF=n` in `config/zen-r4-split/corne_left.conf` and rebuild the left.
15. For exact memory use: `west build -d build/split-left -t ram_report` and `-t rom_report`.

**Later**

- Merge this branch and `main` into `boardsource-wireless-corne`, and remove `CONFIG_ZMK_STUDIO=n` from the Boardsource left config.
- Apply the locked StenoSquid pin map from the Boardsource-StenoSquid README to `qspi.dtsi`.
- Remove or gate the items under [Debug and test code](#debug-and-test-code-to-remove-or-gate).
- Scrub the Zen R4 names after the merge.

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

The left half enters deep sleep after 15 minutes without key activity while USB power is disconnected. It lets any in-progress dictionary save finish, stops QSPI, puts its signal pins in a high-impedance state, and switches off the flash rail and breakout LED. With `CONFIG_ZMK_JAVELIN_STENO_FLASH_IDLE_OFF`, which is on by default, the flash also powers down after a period without dictionary use on the typing and Plover HID layers. `CONFIG_ZMK_JAVELIN_STENO_FLASH_IDLE_SECONDS` sets that period and defaults to 30. The flash stays on for as long as the steno layer is selected, including during practice. Selecting the steno layer starts the flash again, and so does an editor command on another layer, which waits while it starts. Setting the option to `n` keeps the flash on whenever the keyboard is awake. Idle power-down has not been tested on hardware.

Press a key on the left half to wake it. Startup restores flash power and checks the base dictionaries before onboard Javelin and the editor can use them. Start-up runs a quick check: the chip ID, the image headers and native lookups in each base. The full check, which reads every byte of both base images over four data lines and compares their CRCs, runs after provisioning, on the first boot of firmware built for different images, and whenever the quick check fails. A passed full check is recorded in the keyboard's settings, and serial provisioning removes that record before it writes. The quick check does not notice damage in the middle of an image. The base images are only written by provisioning, and the personal dictionary keeps its own CRC-checked snapshots. If the flash does not answer, start-up tries again up to three times, cutting the flash rail for half a second between tries. Until the check finishes, the right display shows **Loading...** on the steno layer, and practice starts by itself once the dictionaries are ready. On the earlier firmware, the breakout LED went out in deep sleep and onboard Javelin translated successfully after a keypress wake. Powered USB insertion is also expected to wake the controller. [nRF52840 power specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/power.html)

## External QSPI flash

This project implements the W25Q128 flash integration on the left half, including QSPI setup and quad reads, serial provisioning with readback, and memory-mapped dictionary lookup from Javelin. The firmware checks each base image's CRC and verifies native lookups before enabling onboard translation. This puts both full base dictionaries in the 16 MiB external chip while the firmware fits in the nice!nano's internal flash.

| External flash contents | Outlines | Size |
| --- | ---: | ---: |
| Lapwing Base | 114,885 | 1,700,488 bytes |
| Plover Base | 147,424 | 2,178,864 bytes |
| Lapwing extras: commands, numbers, UK additions and proper nouns, each with its own checkbox | 29,094 | 451,088 bytes |
| Personal dictionary | Varies | 16 KiB of entry data, backed by two CRC-checked snapshots |

Lapwing is active by default, with its commands, numbers and proper nouns. UK additions start off. See [Lapwing extras](config/zen-r4-split/README.md#lapwing-extras) for the upload this needs. Enable **Plover Base** and disable **Lapwing Base** in the browser to use Plover's base for onboard translation. The keyboard remembers the selection. When enabled, personal entries take priority over both bases. If both bases are active, Lapwing wins on overlapping outlines.

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

## Automatic builds

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds the split firmware on GitHub for every push that changes something other than documentation, and on demand from the **Actions** tab of the repository with **Run workflow**. It runs four builds, and each passes or fails by itself:

| Build | What it is |
| --- | --- |
| `left-logging` | Left half with the diagnostics serial port, for provisioning |
| `left-studio` | Left half with ZMK Studio over USB and Bluetooth |
| `right` | Right half |
| `right-nice-view` | Right half with the nice!view |

Each run prints the flash and RAM totals in its log and attaches the `.uf2` files under **Artifacts** on the page for that run. All four targets passed in [the build for `2ac81ae`](https://github.com/disarmyouwitha/zmk-javelin-steno/actions/runs/43133561440).

## ZMK Studio

The `left-studio` workflow target enables ZMK Studio over USB and Bluetooth. USB keymap reads and key labels were verified on the updated build. Bluetooth editing and renames on the right display were verified on the earlier Studio build.

- The typing, steno and Plover modes are recognised by layer ID, defined in [`javelin_steno_layers.h`](include/dt-bindings/zmk/javelin_steno_layers.h). Renaming those layers in Studio leaves the right display, practice and flash handling working. Reordering should too, since the pinned ZMK keeps a layer's ID when it moves, but that is untested.
- Practice has no layer of its own. It runs on the steno layer and the `&javpractice` key turns it on and off, so the steno keys are mapped once. A steno layer built from scratch on a new layer still types, but gets no dictionary pre-wake or loading message.
- A layer renamed in Studio shows its new name on the right display, cut to 9 characters, and so does a layer added in Studio. **Loading...** and **No dictionary** still take over on the steno layer. A layer that keeps its keymap name shows the built-in label such as **Typing** or **Javelin**, and so does a layer with no name. A rename shows after the next layer change. Both halves need this firmware.
- The split keymap reserves two spare layers, `extra1` and `extra2`, for Studio to hand out. Builds without Studio ignore them.
- Studio is on by default for the left half: `corne_left.conf` sets `CONFIG_ZMK_STUDIO=y`, and `CONFIG_ZMK_STUDIO_LOCKING=n` so no unlock key is needed. Every left build offers Studio over Bluetooth. For Studio over USB, build with `-S studio-rpc-usb-uart` in place of `-S zmk-usb-logging`; that snippet also keeps every behavior available for Studio to assign.
- The typing layer shows tap actions in the Studio grid. Javelin uses a visible key label and a separate dropdown for each of its 38 physical steno keys. See the [split guide](config/zen-r4-split/README.md#studio-key-labels) for the keymap update steps.
- The build fails if the keymap's layer order no longer matches `javelin_steno_layers.h`.

### Studio checks still to run on hardware

- Confirm the named Plover HID keys can be reassigned in Studio.
- Editing a typing key leaves Javelin, Plover HID and practice working.
- Studio's USB serial port works alongside the browser dictionary editor. Studio and the logging port used for provisioning are separate builds.
- Confirm a saved Studio binding survives a reboot.
- Renaming and reordering layers keeps the right display and practice correct.
- Browser editor: disconnect mid-command, reconnect, switch between USB and Bluetooth, and resume editing.

## Debug and test code to remove or gate

These exist for bring-up and battery testing. Before calling the firmware finished, remove each one or put it behind a build flag.

| Item | Where | Current state |
| --- | --- | --- |
| `W` serial query: last flash check path and its time in milliseconds | `src/zen_r4/lapwing_flash.c` | Only in builds with `-S zmk-usb-logging` |
| Diagnostics: boot log kept across a soft reset, `S` status, `D` log dump, `Z` clear, fault counters saved in settings, wake timing, `upload.py --diag` | `src/zen_r4/diag.c`, `src/zen_r4/diag.h`, the `diag_*()` calls in `src/zen_r4/lapwing_flash.c` | Behind `CONFIG_ZMK_JAVELIN_DIAG`, default on, only in builds with `-S zmk-usb-logging`. To remove for good, delete the two files and the calls |
| Battery and activity logger, its `B` serial export and host script | `src/zen_r4/battery_log.c`, `tools/zen-r4-battery/` | Behind `CONFIG_ZMK_JAVELIN_BATTERY_LOG` |
| 128 KiB internal flash partition reserved for the battery log | `config/zen-r4-split/corne_left.overlay` | Always reserved, even with the logger off |
| Flash idle power-down experiment | `CONFIG_ZMK_JAVELIN_STENO_FLASH_IDLE_OFF` in `Kconfig`, `src/zen_r4/lapwing_flash.c` | On by default. Once battery testing settles it, remove the option or keep it as a plain setting |
| Start-up prints such as `[JAV] ...`, the JEDEC ID, flash check time and idle power-down | `src/engine_init.cc`, `src/zen_r4/lapwing_flash.c`, `src/zen_r4/lapwing_dictionary.cc` | Always compiled, visible only on the logging serial port |
| `CONFIG_STACK_SENTINEL` and `CONFIG_THREAD_NAME` | `config/zen-r4-split/corne_left.conf` | Always on |

Serial provisioning with `upload.py` is not on this list. It stays, and is already limited to builds with the logging snippet.

## License

The Javelin steno engine is licensed under [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).
