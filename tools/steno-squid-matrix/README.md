# Steno Squid left matrix test

This standalone Zephyr application reports the 21 handwired switches over USB CDC. It scans columns active high and reads rows with pull-downs for col2row diodes. It leaves the external flash bus idle.

| Matrix | nice!nano v2 pins in order |
| --- | --- |
| R0–R3 | D8, D9, D21, D20 |
| C0–C5 | D19, D18, D15, D14, D0, D1 |
| R3 thumbs | C3, C4, C5 |

Use the same west workspace, Zephyr SDK and environment as the [QSPI probe](../steno-squid-qspi/README.md). From this directory, set `ws` to the absolute path to that workspace.

```sh
matrix="$PWD"
export ZEPHYR_BASE="$ws/zephyr"
cd "$ws"
west build -s "$matrix" -d "$matrix/build" -b nice_nano@2.0.0/nrf52840 -- \
  "-DBOARD_ROOT=$ws/zmk/app/module" "-DDTS_ROOT=$ws/zmk/app/module" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake" \
  "-DZEPHYR_MODULES=$ws/modules/hal/nordic;$ws/modules/hal/cmsis;$ws/modules/debug/segger"
```

Double-tap reset and copy `build/zephyr/zephyr.uf2` onto `NICENANO`. Open the USB serial port at 115200 baud. Tap each switch and check its coordinates on both `DOWN` and `UP`. After all 21 switches have reported both transitions, the log shows `tested=21/21`. The held mask uses bit `row * 6 + column`.

For rollover, hold a chord that spans rows and columns, then check that the held mask contains exactly those switches. Repeat with a thumb included. Release every switch and verify `held=0x000000` and `count=0`.

## Assembled left-half results

The read-only QSPI probe returned JEDEC `EF 40 18` and read 16 bytes at address zero. The standalone matrix tester registered individual presses and releases on all 21 switches. It held R0C0 + R1C1 + R2C2 at `0x004081` and all three plus R3C4 at `0x404081`. The final mask was `0x000000`.
