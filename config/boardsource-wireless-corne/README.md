# Boardsource Wireless Corne SMT bring-up

This port targets the integrated nRF52840 Boardsource Wireless Corne SMT pair. It uses the vendor's six-column matrix, split roles, bootloader partition layout, battery ADC, and USB configuration. The left runs the existing ZMK/Javelin central with external Lapwing and Plover bases. The right runs the split peripheral with nice!view and receives the active mode and Practice display state over Bluetooth.

## Expansion header

Number the connector by the vendor pinout and verify pin 1 on each physical board. Both halves have their own expansion rail. [Boardsource firmware and pinout](https://github.com/boardsource/wireless-corne_zmk_config/tree/2158104902e164c317e35f53dfa0ca1e36e1a751)

| Header pin | GPIO or rail | Left W25Q128 breakout | Right nice!view |
| --- | --- | --- | --- |
| 1 | P0.07 | CS# | |
| 2 | P0.21 | IO0 | |
| 3 | P0.12 | IO3 | MOSI |
| 4 | P0.23 | IO1 | SCK |
| 5 | Switched VCC, P0.31 control | 3V | VCC |
| 6 | GND | G | GND |
| 7 | P0.19 | CLK | CS, active high |
| 8 | P0.05 | IO2 | |

The [left QSPI overlay](qspi.dtsi) disables SPI2, uses high drive in both directions, pulls IO2 and IO3 high in the active state, and sets all six pins to low-power inputs with pulls disabled for sleep. The existing QSPI driver owns P0.31, powers up the flash before enabling its signal pins, and removes flash power during deep sleep. The right uses the vendor's SPI2 pinctrl and the standard ZMK external-power driver on P0.31. The pinned vendor devicetree declares SPI2 but has no P0.31 power driver. Both overlays assume **active-high** rail enable pending measurement.

The owner locked the Steno Squid pin map on 2026-11-11. Clock uses Nordic’s recommended P0.19 QSPI/SCK pin. IO0 and IO1 use P0.21 and P0.23, which Nordic marks for QSPI. IO2 remains on P0.05, IO3 moves to P0.12, and CS# stays on P0.07. Both active and sleep pinctrl states use this map. Begin hardware tests at the configured 2 MHz. [Nordic nRF52840 pin assignments](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/pin.html)

The Adafruit #5634 schematic routes IO2 and IO3 to separate pads. Check continuity on the actual breakout and confirm neither pad is shorted to VCC. Use eight jumper connections to the locked table above for the Adafruit proof of concept. Confirm orientation, pitch and clearance before mounting. [Adafruit breakout pinout](https://learn.adafruit.com/adafruit-qspi-dip-breakout-boards/pinouts) · [Adafruit breakout schematic](https://github.com/adafruit/Adafruit-QSPI-DIP-Breakout-PCB)

## Build

Use the repository's [pinned west manifest](../west.yml) and Zephyr SDK 0.16.9. The [Lapwing preparation commands](../zen-r4-left/README.md#build-and-provision) produce `build/lapwing/lapwing-base.bin`. The left builds use the external images' size and CRC. The UF2s carry the firmware, and the dictionary files are transferred separately when provisioning.

Run from this repository's root in a shell with west and the Zephyr SDK available. Set `ws` to the absolute path of the pinned west workspace.

```sh
repo="$PWD"
target="$repo/config/boardsource-wireless-corne"
zen="$repo/config/zen-r4-split"
probe="$repo/tools/zen-r4-qspi"
vendor="$repo/build/boardsource-upstream"

git clone https://github.com/boardsource/wireless-corne_zmk_config "$vendor"
git -C "$vendor" checkout --detach 2158104902e164c317e35f53dfa0ca1e36e1a751
python3 "$repo/tools/zen-r4-lapwing/prepare_plover.py"
export ZEPHYR_BASE="$ws/zephyr"
zephyr_cmake="$ZEPHYR_BASE/share/zephyr-package/cmake"
cd "$ws"

build_left() {
  output="$1"
  shift
  west build -s zmk/app -d "$repo/build/$output" -b blecorne_left "$@" -- \
    "-DBOARD_ROOT=$vendor" "-DZephyr_DIR=$zephyr_cmake" \
    "-DZMK_CONFIG=$target" "-DZMK_EXTRA_MODULES=$repo" \
    "-DEXTRA_CONF_FILE=$zen/corne.conf;$zen/corne_left.conf" \
    "-DZMK_JAVELIN_LAPWING_IMAGE=$repo/build/lapwing/lapwing-base.bin" \
    "-DZMK_JAVELIN_PLOVER_IMAGE=$repo/build/plover/plover-main.bin"
}
build_left boardsource-left
build_left boardsource-provision -S zmk-usb-logging

west build -s zmk/app -d "$repo/build/boardsource-right" -b blecorne_right -- \
  -DSHIELD=nice_view "-DBOARD_ROOT=$vendor" "-DZephyr_DIR=$zephyr_cmake" \
  "-DZMK_CONFIG=$target" "-DZMK_EXTRA_MODULES=$repo" \
  "-DEXTRA_CONF_FILE=$zen/corne.conf;$zen/right-nice-view.conf"

build_probe() {
  output="$1"
  shift
  west build -s "$probe" -d "$repo/build/$output" -b blecorne_left -- \
    "-DBOARD_ROOT=$vendor" "-DDTS_ROOT=$ws/zmk/app" "-DZephyr_DIR=$zephyr_cmake" \
    "-DDTC_OVERLAY_FILE=$probe/boardsource.overlay" \
    "-DKCONFIG_ROOT=$probe/Kconfig.boardsource" \
    "-DEXTRA_CONF_FILE=$probe/boardsource.conf" "$@"
}
build_probe boardsource-probe
build_probe boardsource-quad -DQUAD_PROBE=ON
```

| Output under `build/` | Use |
| --- | --- |
| `boardsource-probe/zephyr/zephyr.uf2` | First left-side probe. JEDEC and single-line FASTREAD only. |
| `boardsource-quad/zephyr/zephyr.uf2` | Follow-up left probe. Compares single-line and quad reads, using a volatile QE update when required. |
| `boardsource-right/zephyr/zmk.uf2` | Right split peripheral with nice!view. |
| `boardsource-left/zephyr/zmk.uf2` | Everyday left central, including browser HID over USB and Bluetooth. |
| `boardsource-provision/zephyr/zmk.uf2` | Same left target with USB logging and the serial base-dictionary uploader interface. |

The factory application partition is `0x1000–0xD3FFF`. The settings partition starts at `0xD4000` and the UF2 bootloader starts at `0xF4000`. The vendor board keeps USB HID on the central and uses ADC channel 2 for its battery divider. This port uses the fixed keymap with Studio disabled and leaves battery test logging out.

## Test on the new pair

1. **Establish recovery.** Save the matching [factory left and right UF2s](https://github.com/boardsource/wireless-corne_zmk_config), confirm stock typing and split pairing, and check that double-tap RESET mounts each integrated board's bootloader over USB. Record each `INFO_UF2.TXT` and flash one identified half at a time.
2. **Measure the empty header.** Confirm orientation, ground, and VCC with the accessories disconnected. Load the first left probe. Open its USB CDC serial port at 115200 with DTR asserted, which starts the probe and asserts P0.31. Verify the enabled rail is suitable for 3.3 V flash and logic. Check rail rise and behavior on USB and battery, then repeat under load. Verify the nice!view rail and connector orientation separately.
3. **Read the flash ID and sample.** With power off, attach the flash to the left header. Reboot, open the probe's serial port, and expect `JEDEC ID: EF 40 18` followed by a 16-byte `FASTREAD` at address zero. Compare against known bytes when available. An erased chip normally reads all `FF`. The first probe issues neither write-enable nor program/erase commands.
4. **Check quad reads.** Use the separate quad probe only after the rail and JEDEC checks. It compares standard SPI and quad reads over the first 64 KiB. Use a spare, independently preloaded reference chip with data exercising both bit states on all four lanes and look for a pass with lane mask `0xf`. An all-`FF` comparison is inconclusive. This build may temporarily set and restore the flash's volatile Quad Enable bit. It leaves the flash array alone.
5. **Test mapped reads and the split.** A reference flash with both expected bases lets you test Javelin before any provisioning through the keyboard. Flash the matching right UF2 and the provisioning left UF2. Use [upload.py](../../tools/zen-r4-lapwing/upload.py) with the serial port alone for a read-only status query and look for `MAPPED`. Confirm ordinary typing on both halves, onboard `KAT` → “cat” with host Plover closed, raw Plover HID strokes, and nice!view mode and Practice updates. If you have only a blank flash, complete the electrical checks first and return to dictionary-dependent checks after initial provisioning.
6. **Test wireless and sleep.** Pair **Boardsource Corne Steno** and unplug USB. Test ordinary typing, Plover HID, onboard Javelin, and Javelin Steno Tools **Connect via HID**. Let both halves reach deep sleep. Check that the left rail turns off, wake each half with one of its own keys, and check display and split reconnection, flash verification, and onboard output. Use the left provisioning console command `W` to distinguish `CHECK_QUICK` from `CHECK_FULL`. Check right display rail behavior and battery readings independently.
7. **Provision and test persistence.** Once the power and read checks pass, use the [existing dual-base provisioning procedure](../zen-r4-split/README.md#flash-regions-and-initial-plover-provisioning) with the Boardsource provisioning UF2 and its serial device. Send the Lapwing image to `--base lapwing` and the Plover image to `--base plover`. Check for `MAPPED` after both bases verify. Switch bases in the browser, save a temporary personal edit, reboot, verify persistence, and remove the test entry. Switch to the everyday left UF2 and repeat the HID and wake checks.

Compilation and generated pin/partition checks cover the firmware configuration. On-device boot and recovery, rail polarity and voltage, connector fit, all flash reads and writes, battery calibration, Bluetooth, nice!view behavior, and sleep/wake on the Boardsource pair still need physical verification.

## Steno Squid LED

The first adapter can leave the WS2812C-2020-V6 unpopulated. The left header uses all six signal pins for QSPI, and P0.31 controls the shared accessory rail. Keep the LED data off the flash signals. An independently powered or locally controlled LED could provide standalone effects, with its own power budget and voltage requirements. For firmware-controlled effects, a future board revision can expose a dedicated data pin and an appropriate LED supply. A simple rail indicator could instead use a low-current LED and resistor, at the cost of extra current whenever flash power is on.
