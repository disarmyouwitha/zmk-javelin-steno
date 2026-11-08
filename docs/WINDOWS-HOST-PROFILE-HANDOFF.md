# Codex handoff: debug Windows accent output

The Zen R4 split is flashed with the host profile picker. Mac output passed the user's physical test: they could type the accented phrase twice, repeat it, undo with star, and then type `cat`. The user reports the equivalent behavior is incorrect on Windows. The exact Windows output and first failing step have not been recorded yet.

## Start here on Windows

1. Plug the handwired left half into Windows over USB and keep the Corne Ultralight right half powered. In Chrome, open [Javelin Steno Tools → Host Layout](https://lim.au/#/software/javelin-steno-tools/host-layouts), connect to **Zen R4 Split** via HID, and select **Windows U.S. (Alt codes)**. Keep Windows on its ordinary U.S. input layout. Confirm Num Lock is on.
2. In a blank Notepad document with Plover closed, tap the middle right thumb from normal typing to enter onboard Javelin. Stroke `KAT` for **cat** as a baseline, then tap star to clear it. Stroke `SA / SRA` twice, tap star once, then stroke `KAT`. Capture the exact text after each step. Expected progression is **ça va**, **ça va ça va**, **ça va**, **ça va cat**. Also try tapping the number bar instead of writing the second phrase to test the repeat command.
3. Compare against a physical keypad on the same Windows machine, if available. Hold **left Alt**, type **0231** on the keypad, and release Alt. Expected output is **ç**. If the manual sequence fails, check Num Lock, input locale, and the target app. If it works and the Zen fails, compare the actual USB reports or use the firmware's USB diagnostics serial port at 115200 baud to see the emitted key events.

The firmware should press left Alt, tap keypad **0 2 3 1**, and release Alt before typing the rest of the phrase. Look for a missing modifier release, dropped or coalesced reports, or undo counting a character that Windows did not insert. These are hypotheses to check once the exact symptom is known.

## Code and verification

- Repository: `disarmyouwitha/zmk-javelin-steno`. Check out `main` to get the host profile code and this handoff. Firmware commit `879e6e5` adds the feature and `a7ab6f8` records the power-cycle check. The board already has the feature build.
- `src/platform/windows_us_layout.cc` maps common Windows-1252 characters to leading-zero Alt+numpad sequences. `src/platform/zmk_key_shim.cc` suppresses the zero-key sentinel after Javelin releases Alt. `src/engine_init.cc` selects the layout and persists it as `javelin/host_profile` in ZMK settings.
- Chrome successfully switched both profiles over HID. The Windows selection survived a USB power cycle. Native emitter tests verified `Alt+0231` for **ç**, `Alt+0233` for **é**, and Alt release between consecutive accents. Those tests did not exercise the USB report path on Windows.
- The website message about updating host layouts refers to uploading layout definitions. Selecting a built-in profile already works. Keep debugging focused on Windows text output.
- The tested left UF2 is `build/browser-left/zephyr/zmk.uf2` in the Mac checkout, SHA-256 `7090d5108c7aa2e00bb3d4ab2015e09b7f265f0a98774d41f1800e5c23242204`. Reproducing the issue should not require reflashing or reprovisioning either dictionary.
