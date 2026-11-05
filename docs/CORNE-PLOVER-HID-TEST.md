# Corne: Javelin plus Plover HID

All test configuration and source changes live in this repository. No CI workflow is included. The Zen repository is a read-only reference; it is not the build workspace or PR target.

## Modes

- Main preserves the Zen keymap except the middle right thumb now enters Plover HID instead of sending a second Space. Sym+Num is unchanged.
- From Main, tap the middle right thumb (the circled second Space) to enter host Plover HID (layer 3).
- From Main, tap the outer right thumb to enter onboard Javelin (layer 2). The inner right thumb remains Space.
- In Javelin, tap the same outer right thumb to enter host Plover HID (layer 3).
- In Plover HID, tap that thumb to return to Javelin.
- In either steno layer, the thumb immediately left of A returns to Main.
- All steno positions, including the individual number/star/S keys, match the Zen mapping.

Plover HID mode forwards raw steno key state to the host; it does not also invoke Javelin. Onboard Javelin still uses only the four-word internal-flash dictionary: TP=if, -T=the, KAT=cat, S=is.

## Build and hardware

`config/west.yml` pins ZMK, the Javelin engine, and the Plover HID dependency. Build locally with west, CMake, Ninja and the Zephyr SDK installed. From a separate west workspace, initialize from a copy of this repository's config directory, update dependencies, then build with this repository supplied as `ZMK_EXTRA_MODULES`:

```sh
# Set these to the absolute module repo and west workspace paths.
# The workspace must already be initialized and updated from config/west.yml.
west build -s zmk/app -d build/corne-left -b nice_nano//zmk -S zmk-usb-logging -- -DSHIELD=corne_left -DZMK_CONFIG="$JAVELIN_REPO/config" -DZMK_EXTRA_MODULES="$JAVELIN_REPO"
west build -s zmk/app -d build/corne-right -b nice_nano//zmk -- -DSHIELD=corne_right -DZMK_CONFIG="$JAVELIN_REPO/config" -DZMK_EXTRA_MODULES="$JAVELIN_REPO"
```

The CMake file accepts either a nested engine checkout or the adjacent pinned west checkout. Engine compatibility patches use Python so they work on macOS as well as Linux. If Zephyr has not been exported into the CMake package registry, add `-DZephyr_DIR="$ZEPHYR_BASE/share/zephyr-package/cmake"` to each build's CMake arguments. Set `ZEPHYR_BASE` to the workspace's Zephyr checkout and `ZEPHYR_SDK_INSTALL_DIR` to the SDK directory.

The left/central USB build has two HID interfaces: normal keyboard HID_0 and Plover HID_1. It also includes USB serial diagnostics. This test reuses the same upstream Plover HID module revision as the Zen. Host Plover still needs a compatible Plover HID machine/plugin. USB Plover HID is the first validation target; Bluetooth Plover HID behavior is not established by this test.

## Validation plan

1. Build both Corne halves.
2. Verify onboard Javelin still types "if the cat is the cat" from TP / -T / KAT / S / -T / KAT.
3. Switch to Plover HID, verify the USB steno HID interface and raw reports, and confirm translation through host Plover.
4. Return to Javelin and Main without sticky held keys.

The earlier Javelin-only firmware passed its physical phrase test. Both combined-mode UF2s compiled locally on macOS using Zephyr SDK 0.16.9 (GCC 12.2), CMake 3.31.10, Ninja 1.13.2 and west 1.5.0. The UF2 verifier passed for both halves; the left's actual linked dictionary resolves the exact phrase above. The left build enables both HID interfaces and links the Plover HID driver. Left usage: 468,604 bytes flash / 173,064 bytes RAM; right: 192,200 bytes flash / 69,284 bytes RAM. Combined-mode hardware testing remains pending. Raw USB logs stay local.

Flash both halves with their corresponding UF2s for this test. Connect the left half by USB for initial Plover HID validation. No settings-reset firmware is included or required by this build.
