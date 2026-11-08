# Zen R4 and Corne Ultralight split

The handwired Zen R4 left half is the Bluetooth split central and the Corne Ultralight right half is the peripheral. Each half scans its own wiring and reports positions in the same 42-key Corne layout. The left half handles USB typing, host Plover HID, and onboard Javelin from the base dictionaries on the W25Q128.

The split uses the [Corne-ish Zen Voyager layout](https://github.com/disarmyouwitha/zmk-config-zen-plover-hid/blob/6a3cf7dbf6502571d5211078ba18009e31b938e6/config/corneish_zen.keymap) with direct access to both steno modes. In MAIN, the left thumbs are Command, hold for SYM+NUM, and Backspace. The right thumbs are Space, onboard Javelin, and Plover HID. Both lists are in physical left-to-right order.

In either steno mode, the middle right thumb chords U, the outer right thumb switches steno modes, and the outer left thumb returns to MAIN.

## Build

Use the pinned west workspace and Lapwing image from the [standalone left-half guide](../zen-r4-left/README.md). Run from the repository root with `ws` set to the absolute path of that workspace.

```sh
repo="$PWD"
python3 "$repo/tools/zen-r4-lapwing/prepare_plover.py"
export ZEPHYR_BASE="$ws/zephyr"
cd "$ws"
west build -s zmk/app -d "$repo/build/split-left" -b nice_nano//zmk -S zmk-usb-logging -- \
  -DSHIELD=corne_left "-DZMK_CONFIG=$repo/config/zen-r4-split" "-DZMK_EXTRA_MODULES=$repo" \
  "-DZMK_JAVELIN_LAPWING_IMAGE=$repo/build/lapwing/lapwing-base.bin" \
  "-DZMK_JAVELIN_PLOVER_IMAGE=$repo/build/plover/plover-main.bin" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake"
west build -s zmk/app -d "$repo/build/split-right" -b nice_nano//zmk -- \
  -DSHIELD=corne_right "-DZMK_CONFIG=$repo/config/zen-r4-split" "-DZMK_EXTRA_MODULES=$repo" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake"
```

The Plover image argument is optional for older Lapwing-only builds. The preparation script downloads a pinned official Plover `main.json`, checks its source checksum, compiles it for address `0x12200000`, and verifies every outline against the compiled image. Numeric outlines include the number bar and corresponding physical keys, matching the pinned engine's stroke parser.

## Select an onboard base dictionary

The dual-base build lists **user_dictionary**, **Lapwing Base**, and **Plover Base** in the existing Javelin browser tools' **Active Dictionaries** controls. On a keyboard without a saved selection, the personal dictionary and Lapwing are enabled and Plover is disabled. Browser enable, disable, and toggle commands save the selection in ZMK's settings before acknowledging success. The selection survives reboot and application UF2 updates. A settings reset clears it, along with other ZMK settings.

Dictionary selections are stored on the keyboard and shared across connected computers. The HID console supports the dictionary-status events used to refresh the browser checkboxes. Individual checkboxes and All Dictionaries were verified in Chrome, including a fresh reconnect.

To use Plover's onboard base, enable **Plover Base** and disable **Lapwing Base**. To return to Lapwing, enable **Lapwing Base** and disable **Plover Base**. Personal entries have highest priority when enabled. If both bases are enabled, Lapwing takes priority for overlapping outlines, followed by Plover. These switches affect onboard Javelin. Host Plover HID still sends raw strokes to Plover on the computer.

The existing website supplies these controls. Firmware support handles listing, selection, and persistence. Both bases remain read-only through the browser, and the personal dictionary remains editable.

### Flash regions and initial Plover provisioning

| Contents | External flash offset | Image size | CRC32 |
|---|---|---|---|
| Lapwing Base, 114,885 outlines | `0x000000` | 1,700,488 bytes | `720cfca6` |
| Plover Base, 147,424 outlines | `0x200000` | 2,178,864 bytes | `14b65522` |
| Personal snapshots | `0xF00000` | Two 28 KiB slots | Per-snapshot CRCs |

Plover source: [official main.json at the pinned revision](https://github.com/openstenoproject/plover/blob/8a52c5296c502507f9a46820a4a947ca25f998e6/plover/assets/main.json). Source SHA256: `6570873c7f703ecd39c9870083f643db3475b49462a8db80fe22fe33ac3cfe92`. Compiled padded image SHA256: `6d74757af03ad1769f6f2d587afcc8957a5486b3e31ed3e20f00410c2624e2c4`.

After flashing the dual-base left UF2, program Plover over the diagnostics port, substituting the actual device path:

```sh
python3 tools/zen-r4-lapwing/upload.py /dev/cu.usbmodem14501 \
  --base plover build/plover/plover-main.bin
python3 tools/zen-r4-lapwing/upload.py /dev/cu.usbmodem14501 --base plover
```

Provisioning checks the complete transfer before programming, verifies each programmed page, then checks the complete raw and mapped CRCs and native Javelin lookups. It writes only the selected base's region, performs no erases, and rejects conflicting pre-existing bits. Existing Lapwing and personal snapshots remain in place. The dual-base build starts onboard Javelin after both configured bases verify. `VERIFIED` means that base is intact but another configured base still needs provisioning. `MAPPED` means both bases passed and are available to Javelin. Only the left half needs updating for this feature.

## Flash and check

Flash `build/split-right/zephyr/zmk.uf2` to the Ultralight first and keep the right half powered. Then flash `build/split-left/zephyr/zmk.uf2` to the handwired left half. Keep the left connected to the host over USB. The same Lapwing image already on the left half's external flash should verify and load at boot. Use the [Lapwing uploader](../../tools/zen-r4-lapwing/upload.py) with only the USB serial port argument to check for `MAPPED`.

Check ordinary typing on both halves, then test `TP / -T / KAT / S / -T / KAT` in Javelin with host Plover closed. Use the right half for the final `-T`. Return to MAIN and check the same cross-half chords in Plover HID.

## Pairing an Ultralight previously used in another split

On the tested pair, the left central scanned without finding the right half until the right controller's old Bluetooth settings were cleared. Build the standard `settings_reset` shield for nice!nano v2 and flash it to the Ultralight. When it boots as SETTINGS RESET, return the right half to bootloader mode and flash the split-right image again. Keep the left central powered so it can discover and pair with the right half. The central log confirmed the encrypted connection and its subscription to right-half key positions.

The builder confirmed normal typing over the split, onboard Javelin output of “cat” from a physical `KAT` chord spanning both halves, and receipt of the same chord in host Plover HID. The left also verified and loaded the existing Lapwing image after reboot.

## Edit the onboard personal dictionary

The left firmware exposes a dedicated HID console for [Javelin Steno Tools](https://lim.au/#/software/javelin-steno-tools). With the left connected over USB, open the tools in Chrome and choose **Connect via HID**. Select **Zen R4 Split**, then open **Dictionary Management → View**. This console uses Javelin's vendor HID collection, alongside the ordinary keyboard and Plover HID interfaces. A single USB serial port remains for diagnostics and Lapwing provisioning.

Use **Add Translation** to enter an outline and translation, then press Return in the translation field to save. Add, replace, remove, or export personal outlines from the browser. A saved entry takes precedence over the same Lapwing outline in onboard Javelin. Removing the personal entry restores the Lapwing translation. Host Plover HID continues to send raw strokes to the host.

Personal entries are backed by two CRC-checked snapshots in a reserved region of the W25Q128 starting at `0xF00000`. The editor acknowledges an edit after a verified flash write. The previous snapshot remains available if power is interrupted before the next save completes. The personal dictionary has 16 KiB for entry data. To reclaim space from replaced and deleted entries when it fills, export the dictionary, reset it, and import the export. Lapwing base occupies its own read-only region, and ZMK continues to run the matrix, split Bluetooth, and host USB interfaces.

The first browser firmware passed its Javelin Web Serial handshake, personal dictionary export, add, replace, and outline lookup checks on the assembled keyboard. The official tools in Chrome displayed the personal dictionary and saved a replacement translation. That edit survived a controller reset. Removing the test entry restored the Lapwing translation. A host simulation of the storage code also checked interrupted saves, corrupt-snapshot fallback, and write boundaries.

The HID transport passed the native Javelin handshake, commands spanning multiple reports, dictionary exports, and two interleaved console channels. The official tools in Chrome connected through **Connect via HID** and saved a translation through the browser. That entry survived a firmware flash and reboot, and deleting it restored Lapwing's translation. This build exposes three HID interfaces and one diagnostics serial interface.

The builder confirmed all three keyboard modes on the final HID firmware, including normal typing, host Plover HID, and onboard Javelin across both halves. The physical steno test produced “the cat is the cat if the cat is the cat!”

After the editor firmware was flashed and rebooted, the builder reconfirmed normal typing, onboard Javelin, and host Plover HID on both halves.

The dual-base firmware built at 472,520 bytes of application flash and 227,368 bytes of RAM. Host verification checked all 114,885 Lapwing outlines and all 147,424 Plover outlines against their mapped binaries. On the assembled left half, provisioning passed preflight, program/readback, and both base CRC/native lookup checks. HID checks verified initial Lapwing selection, Plover selection (`STKERT` → “dessert” and `12K` → “12:00”), Lapwing priority when both bases are enabled (`STKERT` → “desert”), toggling, rejection of unknown dictionary names, and unchanged personal dictionary contents.

A controller reset with Plover selected reloaded Plover as enabled and Lapwing as disabled. `STKERT` still resolved to “dessert” after reboot. The test then restored and saved Lapwing enabled with Plover disabled, and `STKERT` returned “desert” again.

## Lapwing repeat command

The firmware recognizes the bare `=repeat_last_translation` macro used by Lapwing's number-bar-only (`#`) outline. It repeats the complete preceding translation, including multi-stroke and multi-word definitions, while retaining formatting commands. Repeated presses repeat the translation again, and undo removes a repeat. With no previous translation it produces no text. The dictionary entry itself remains unchanged, so dictionary-inspection tools still display the macro string.

The build patches a private copy of the pinned segment builder. It leaves the shared engine checkout and external base images unchanged. The behavior follows the [Plover Last Translation macro](https://github.com/nsmarkop/plover_last_translation/blob/master/plover_last_translation/macro.py). This implements the bare macro used by Lapwing, rather than the plugin's optional count arguments.

Native engine tests capture keyboard events and cover empty history, single-stroke words, multi-stroke words, multi-word translations, consecutive repeats and undo, suffixes, glued fingerspelling, punctuation, and a 40-repeat sequence. The same tests reproduce literal command output with the original engine. Run them with:

```sh
python3 tools/tests/run_repeat_translation.py "$ws/javelin-steno"
```

## Onboard spelling and host output profiles

Javelin applies common English spelling rules to separate suffix strokes. For example, `PWAOEUBG / -G` produces “biking.” A matching dictionary outline takes precedence over these rules.

With Javelin Steno Tools connected over HID, open **Host Layout** and select the profile for the connected computer. The left half saves it in ZMK settings and reloads it after reboot. The setting affects onboard Javelin text output.

| Profile | Host setup | Character output |
| --- | --- | --- |
| Mac U.S./ABC (default) | Use the U.S. or ABC input source. | Option and dead-key sequences, such as Option+C for ç. |
| Windows U.S. (Alt codes) | Keep the standard U.S. input layout and turn Num Lock on. | Windows-1252 characters through built-in Alt+numpad codes, such as Alt+0231 for ç. |

Try `SA / SRA` in a blank editor for “ça va.” Each profile covers a limited set of characters. Windows may interpret the Alt sequences differently if Num Lock is off, the host uses another code page, or the app intercepts the keystrokes. The picker selects firmware-supplied profiles. The page's layout update section refers to uploading new definitions and can still show that uploads are unavailable.

This split enables `CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT` to provide the Mac profile and, together with the browser dictionary option, the Windows profile picker. Builds with that option disabled use Javelin's plain U.S. output.

Hardware verification covered switching profiles in Chrome over HID and retaining Windows U.S. across a USB power cycle. The Mac passed accented typing, repeat, undo, and subsequent ordinary typing. Engine tests checked the Windows keypad sequences for ç and é and verified that Alt releases between characters. The user reported incorrect output on a Windows host. The [Windows test handoff](../../docs/WINDOWS-HOST-PROFILE-HANDOFF.md) tracks reproduction and diagnosis.
