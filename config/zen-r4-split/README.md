# Zen R4 and Corne Ultralight split

The handwired Zen R4 left half is the Bluetooth split central and the Corne Ultralight right half is the peripheral. Each half scans its own wiring and reports positions in the same 42-key Corne layout. The left half handles USB typing, host Plover HID, and onboard Javelin from the Lapwing dictionary on the W25Q128.

The left thumb controls keep the standalone assignments. From MAIN, the outer thumb enters Javelin, the middle thumb enters Plover HID, and the inner thumb taps Backspace or holds SYM+NUM. The outer thumb exits either steno mode. The right half supplies the right-bank steno keys, so R1C0 and R2C0 on the left return to their regular unused steno positions.

## Build

Use the pinned west workspace and Lapwing image from the [standalone left-half guide](../zen-r4-left/README.md). Run from the repository root with `ws` set to the absolute path of that workspace.

```sh
repo="$PWD"
export ZEPHYR_BASE="$ws/zephyr"
cd "$ws"
west build -s zmk/app -d "$repo/build/split-left" -b nice_nano//zmk -S zmk-usb-logging -- \
  -DSHIELD=corne_left "-DZMK_CONFIG=$repo/config/zen-r4-split" "-DZMK_EXTRA_MODULES=$repo" \
  "-DZMK_JAVELIN_LAPWING_IMAGE=$repo/build/lapwing/lapwing-base.bin" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake"
west build -s zmk/app -d "$repo/build/split-right" -b nice_nano//zmk -- \
  -DSHIELD=corne_right "-DZMK_CONFIG=$repo/config/zen-r4-split" "-DZMK_EXTRA_MODULES=$repo" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake"
```

## Flash and check

Flash `build/split-right/zephyr/zmk.uf2` to the Ultralight first and keep the right half powered. Then flash `build/split-left/zephyr/zmk.uf2` to the handwired left half. Keep the left connected to the host over USB. The same Lapwing image already on the left half's external flash should verify and load at boot. Use the [Lapwing uploader](../../tools/zen-r4-lapwing/upload.py) with only the USB serial port argument to check for `MAPPED`.

Check ordinary typing on both halves, then test `TP / -T / KAT / S / -T / KAT` in Javelin with host Plover closed. Use the right half for the final `-T`. Return to MAIN and check the same cross-half chords in Plover HID.

## Pairing an Ultralight previously used in another split

On the tested pair, the left central scanned without finding the right half until the right controller's old Bluetooth settings were cleared. Build the standard `settings_reset` shield for nice!nano v2 and flash it to the Ultralight. When it boots as SETTINGS RESET, return the right half to bootloader mode and flash the split-right image again. Keep the left central powered so it can discover and pair with the right half. The central log confirmed the encrypted connection and its subscription to right-half key positions.

The builder confirmed normal typing over the split, onboard Javelin output of “cat” from a physical `KAT` chord spanning both halves, and receipt of the same chord in host Plover HID. The left also verified and loaded the existing Lapwing image after reboot.

## Edit the onboard personal dictionary

The left firmware exposes a separate USB serial console for [Javelin Steno Tools](https://lim.au/#/software/javelin-steno-tools). With the left connected over USB, open the tools in Chrome and use the menu beside **Connect to Device** to choose **Connect via Serial**. Select the Javelin serial port for Zen R4 Split, then open **Dictionary Management → View**. The tested Mac listed the editor as `cu.usbmodem14501` and diagnostics as `cu.usbmodem14504`. The suffixes can change with the USB connection. Use the other port if the browser cannot complete its Javelin handshake.

Use **Add Translation** to enter an outline and translation, then press Return in the translation field to save. Add, replace, remove, or export personal outlines from the browser. A saved entry takes precedence over the same Lapwing outline in onboard Javelin. Removing the personal entry restores the Lapwing translation. Host Plover HID continues to send raw strokes to the host.

Personal entries are backed by two CRC-checked snapshots in a reserved region of the W25Q128 starting at `0xF00000`. The editor acknowledges an edit after a verified flash write. The previous snapshot remains available if power is interrupted before the next save completes. The personal dictionary has 16 KiB for entry data. To reclaim space from replaced and deleted entries when it fills, export the dictionary, reset it, and import the export. Lapwing base occupies its own read-only region, and ZMK continues to run the matrix, split Bluetooth, and host USB interfaces.

The browser firmware passed its Javelin Web Serial handshake, personal dictionary export, add, replace, and outline lookup checks on the assembled keyboard. The official tools in Chrome displayed the personal dictionary and saved a replacement translation. That edit survived a controller reset. Removing the test entry restored the Lapwing translation. A host simulation of the storage code also checked interrupted saves, corrupt-snapshot fallback, and write boundaries.

After the editor firmware was flashed and rebooted, the builder reconfirmed normal typing, onboard Javelin, and host Plover HID on both halves.
