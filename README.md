## Zen R4 handwired left half

The assembled 21-key left half has been tested over USB in normal typing, host Plover HID, and onboard Javelin modes. Javelin reads the full Lapwing base dictionary from the W25Q128 QSPI flash. The [handwired left-half guide](config/zen-r4-left/README.md) includes the wiring diagram, build and provisioning steps, and the [tested keymap](config/zen-r4-left/nice_nano.keymap).

## Combined Corne test build

This repository contains its own `config/` and local build instructions. No CI workflow is included. No changes to the separate Zen config repository are needed. See [Corne Plover HID test instructions](docs/CORNE-PLOVER-HID-TEST.md).

From Main, the right thumb cluster is **Space / Plover HID / Javelin Steno** (inner to outer). Plover HID uses the host Plover engine; Javelin uses the onboard tiny dictionary. The thumb left of A exits either steno mode to Main. Combined-mode hardware validation is pending.

# zmk-javelin-steno

The [Zen R4 external flash diagnostics](tools/zen-r4-qspi/) contain the breadboard quad SPI, memory-mapped dictionary, and simulated ZMK keyboard report tests.

ZMK module that integrates the [Javelin steno engine](https://github.com/jthlim/javelin-steno) for on-board stenography on ZMK keyboards.

## Status

**Four-entry internal-flash test confirmed on hardware, November 4, 2026.** A 42-key Corne with nice!nano v2 controllers translated `TP / -T / KAT / S / -T / KAT` to **if the cat is the cat**, with no host Plover and no external flash. Test configuration and results: https://github.com/disarmyouwitha/zmk-config-zen-plover-hid/blob/corne-javelin-test/CORNE-JAVELIN-TEST.md

The Corne target builds a 142-byte, read-only fixture from `dicts/cat-test.json`, embedded as relocatable ARM assembly in `dicts/cat-test.S`. Regenerate the fixture with:

```sh
python3 tools/dict_compiler/compile_dict.py dicts/cat-test.json -o /tmp/cat-test.bin --asm-output dicts/cat-test.S
python3 tools/dict_compiler/test_tiny_dict.py
```

Pin the engine to `22726675b2dcf3b8b6ba1e20898f25e324b2260a`; the JSC4 structure layout is version-dependent. The bridge fixes stroke bit masks, compact-table rank offsets, native pointer relocation, the stroke-table first-entry pointer, Zephyr 4 C++ settings, peripheral behavior registration, and safe lazy engine initialization. Diagnostics verify all four entries through the native engine before translation. Persistent user-dictionary writes remain unimplemented and the RAM placeholder is excluded from this read-only test.

The requirements below are the upstream author's historical build figures. See the handwired left-half guide for the measured external Lapwing image size.

## Flash Requirements

Total flash usage (firmware + engine + dict) on a corne split (central half):

| Dictionary | Total flash |
|------------|------------|
| Plover + Lapwing | 3.78 MB |
| Plover only | 2.53 MB |
| Lapwing only | 2.08 MB |
| No dict (engine only) | 534 KB |

## Making It Work on 1MB Flash

If you want to use this on boards with limited flash (nice_nano_v2, etc.), some options:

- **Optimize dictionary format** — Javelin uses JSC4 hash maps (~2-3MB compiled). A trie/DAWG-based format could reduce this significantly.
- **Reduce dictionary size** — Trim to most common entries. ~120K entries fits ~458KB, ~100K entries fits ~384KB.
- **Add external QSPI flash** — Boards with external flash (W25Q128, etc.) can store the full dictionary. The engine already supports XIP reads via `XipPointer`.

## Setup

Add to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: disarmyouwitha
      url-base: https://github.com/disarmyouwitha
    - name: jthlim
      url-base: https://github.com/jthlim
  projects:
    - name: zmk-javelin-steno
      remote: disarmyouwitha
      revision: 995fa19
    - name: javelin-steno
      remote: jthlim
      revision: 22726675b2dcf3b8b6ba1e20898f25e324b2260a
      path: zmk-javelin-steno/javelin
```

Add to your `.conf`:

```ini
CONFIG_ZMK_JAVELIN_STENO=y
CONFIG_ZMK_JAVELIN_STENO_DICT_PLOVER=n
CONFIG_ZMK_JAVELIN_STENO_DICT_LAPWING=n
CONFIG_CPP=y
CONFIG_REQUIRES_FULL_LIBCPP=y
CONFIG_HEAP_MEM_POOL_SIZE=32768
```

For this tested fixture build, leave both full-dictionary options disabled. The fixture is selected directly in CMake; restoring full dictionary selection is future work.

Add to your `.keymap` (see [steno_keys.h](include/dt-bindings/zmk/javelin_steno.h) for all available key bindings):

```dts
#include <dt-bindings/zmk/javelin_steno.h>
#include <behaviors/javelin_steno.dtsi>

steno_layer {
    bindings = <
        &javsteno STENO_S1  &javsteno STENO_TL  &javsteno STENO_PL ...
    >;
};
```

## License

This module is a bridge layer. The Javelin steno engine itself is licensed under [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).
