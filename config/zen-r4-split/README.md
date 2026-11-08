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
