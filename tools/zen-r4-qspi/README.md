# Zen R4 external flash diagnostics

These bench tests use a nice!nano v2 and a 3.3 V W25Q128 breakout with all four data lanes accessible. The standalone Zephyr probe checks the flash bus and mapped reads. The separate ZMK bench build feeds simulated chords through Javelin and inspects the keyboard reports. These targets use the breadboard pin map.

## Breadboard wiring

| W25Q128 | nice!nano v2 | nRF52840 GPIO |
|---|---|---|
| CS# | D2 | P0.17 |
| CLK | D4 | P0.22 |
| IO0 / DI | D5 | P0.24 |
| IO1 / DO | D6 | P1.00 |
| IO2 / WP# | D7 | P0.11 |
| IO3 / HOLD# | D3 | P0.20 |
| VCC | Switched 3.3 V | P0.13 enables the regulator |
| GND | GND | Common ground |

The modified breakout used for these tests has IO2 and IO3 isolated from their original VCC pads. For quad mode both legs need independent connections. The probe enables the switched rail, waits 50 ms, and uses high drive on all six signal pins. Check the wiring and supply against the actual breakout. [nice!nano v2 schematic](https://nicekeyboards.com/docs/nice-nano/pinout-schematic/) · [Nordic QSPI specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/qspi.html?contentId=SMu_w_g~A4Mqfus~XHl4fQ)

## Build and flash

Use a west workspace initialized from this repository's [pinned manifest](../../config/west.yml), with the Zephyr SDK and west's Python dependencies installed. The tests ran with Zephyr 4.1.0 and SDK 0.16.9. Run the following from this directory. Set `ws` to the absolute path of the initialized workspace and set `ZEPHYR_SDK_INSTALL_DIR` if the SDK is outside the default search path.

```sh
probe="$PWD"
ws="/absolute/path/to/west-workspace"
export ZEPHYR_BASE="$ws/zephyr"

cd "$ws"
west build -s "$probe" -d "$probe/build/scratch" \
  -b nice_nano@2.0.0/nrf52840 -- \
  "-DBOARD_ROOT=$ws/zmk/app/module" "-DDTS_ROOT=$ws/zmk/app/module" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake" \
  "-DZEPHYR_MODULES=$ws/modules/hal/nordic;$ws/modules/hal/cmsis;$ws/modules/debug/segger" \
  -DSCRATCH_PROBE=ON
```

For the standalone probe, copy the generated `zephyr/zephyr.uf2` from the selected build directory to the `NICENANO` drive after double-tapping reset. The drive's `INFO_UF2.TXT` should report `nRF52840-nicenano-v2`. On macOS, open the new USB serial port with `screen /dev/cu.usbmodem14501 115200`, substituting its actual device path. The serial connection triggers a single run after boot. For the trace build, send `t` to trigger the test. To restore normal operation, double-tap reset and flash the previous application UF2.

Each mode should have its own build directory. Use these CMake arguments after `--`.

| Test | Build arguments | What it checks |
|---|---|---|
| Read-only bring-up | Defaults | JEDEC ID and the first 16 bytes in single-line mode |
| Quad read-only | `-DQUAD_PROBE=ON` | Compares single and quad reads with volatile Quad Enable where needed. Uniform data can make this inconclusive. |
| Scratch program, quad read and erase | `-DSCRATCH_PROBE=ON` | Requires a blank 4 KiB sector at `0xFFF000`, writes a known pattern, reads it in both modes and verifies the sector is blank after cleanup. |
| Timing trace | `-DSCRATCH_PROBE=ON -DTRACE_PROBE=ON` | Compares repeated quad reads at standard drive, high drive, and high drive with later sampling. |
| Memory-mapped reads | `-DSCRATCH_PROBE=ON -DXIP_PROBE=ON` | Compares aligned CPU word loads with quad EasyDMA at two scratch-page offsets. |
| Native Javelin lookup | `-DSCRATCH_PROBE=ON -DJAVELIN_PROBE=ON` | Programs the four-entry fixture and looks up six strokes from mapped flash. |

The scratch modes skip writes and erases if the sector already contains data. The Javelin probe prepares a private copy of the pinned reader and uses synchronized, aligned mapped loads. The test board reported nRF52840 anomaly 216. The mapped tests also reactivate QSPI before accessing affected control registers after mapped reads. [Nordic anomaly 215](https://docs.nordicsemi.com/r/bundle/errata_nrf52840_rev3/page/err/nrf52840/rev3/latest/anomaly_840_215.html) · [Nordic anomaly 216](https://docs.nordicsemi.com/r/bundle/errata_nrf52840_rev1/page/err/nrf52840/rev1/latest/anomaly_840_216.html)

## ZMK keyboard report bench

```sh
python3 "$probe/bench/prepare.py" "$ws"
cd "$ws"
west build -s zmk/app -d "$probe/build/zmk-bench" -b nice_nano//zmk -- \
  "-DZMK_CONFIG=$probe/bench/config" \
  "-DZMK_EXTRA_MODULES=$probe/build/bench-module" \
  "-DEXTRA_DTC_OVERLAY_FILE=$probe/app.overlay" \
  "-DZephyr_DIR=$ZEPHYR_BASE/share/zephyr-package/cmake"
```

The build uses a disposable copy of the integration module and produces `build/zmk-bench/zephyr/zmk.uf2`. Its mock scanner lets the bench run without keys. It captures the original reports at the USB write boundary and submits neutral frames to the host. The production ZMK keymap and physical matrix use their own build targets.

## Hardware results

| Check | Observed result |
|---|---|
| JEDEC and single-line read | `EF 40 18` and 16 bytes read at offset zero |
| Quad drive comparison | Standard drive matched 0/8 pages. High drive matched 8/8 at each of the two sampling delays. |
| High-drive quad after reboot | All 256 programmed bytes matched. All four lanes carried both bit values. |
| Memory-mapped quad | All 128 CPU word loads matched EasyDMA across two distinct pages. |
| Native external dictionary | Six injected strokes resolved to **if the cat is the cat**. All returned words pointed into the mapped fixture. |
| Full ZMK bench | 42 press/release reports decoded to the same phrase. Every key was released. |

The scratch sector verified blank after each successful scratch test. These tests cover the breadboard flash and the onboard translation path. The left display, physical matrix, and Plover HID still need combined hardware validation on the final pin map.

## Left nice!view on a custom Zen R4 PCB

The Rev 2 proposal reserves D2–D7 for the tested QSPI bus and uses a separate display SPI bus with D0 for SCK, D1 for MOSI, and the underside P1.07 pad for CS. The 4×6 matrix uses ten other GPIOs, including underside P1.01 and P1.02, and leaves both NFC pins free. The left display needs custom ZMK pinctrl and an accessible controller footprint for all three underside pads. Verify the display module's 14 × 36 mm outline, socket stack height, and case clearance in KiCad. Shared switched VCC must stay on during mapped flash access. [nice!view dimensions and pinout](https://nicekeyboards.com/docs/nice-view/pinout-schematic/) · [Nordic pin assignments](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/pin.html?contentId=hMpuE~dcsjrrIlIt8ErcrQ)
