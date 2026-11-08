# Hybrid split Bluetooth battery test

Branch: `zen-r4/bluetooth-battery-test`, starting from `main` at `c2ba6d4`.

## What was implemented at the time

| Feature | Firmware status | Battery-only hardware test |
| --- | --- | --- |
| Right half to left half | Existing ZMK Bluetooth split connection | Previously passed; repeat with left on battery |
| Normal typing to computer | ZMK BLE keyboard output enabled | Passed on Mac, user confirmed 2026-11-08 |
| Onboard Javelin to computer | Translation uses ZMK keycode events and its selected output | Passed on Mac, including accented output: “ça va ça va” |
| Plover HID to computer | Module implements BLE HID over GATT reports and endpoint routing | Passed on Mac; user confirmed strokes received by Plover |
| Javelin browser dictionary tools | Custom console is currently registered only as USB `HID_2` | Wireless transport not implemented |

The [wireless editor follow-up](JAVELIN-BLUETOOTH-HID.md) added a dedicated editor
interface that works with the existing **Connect via HID** option.

## Pair and test

### Observed Mac pairing issue (2026-11-08)

The keyboard initially appeared as `SETTINGS RESET` in macOS discovery. Clearing
profile 1 alone did not fix pairing: the keyboard rejected the Mac's pairing
request with SMP status `0x03` / ZMK security error 4. Profile 0 still contained a
host bond. Selecting profile 0 and clearing its bond resolved the failure.
The new pairing completed with SMP status `0x00`, encrypted security level 2,
and the Mac then listed the connected keyboard as **Zen R4 Split**. The Mac's
address was saved to profile 0. The user subsequently confirmed normal typing,
Plover HID strokes, and onboard Javelin translation over Bluetooth, including
“ça va ça va”. A full power-cycle/reconnect check and explicit repeat/undo checks
over Bluetooth remain unconfirmed.

Pairing diagnosis used a temporary build option,
`-DCONFIG_BT_SMP_LOG_LEVEL_DBG=y`. Do not retain that option in normal builds or
publish its raw logs: Zephyr's detailed SMP logging can include security keys.

### Test sequence

1. Keep both halves powered. Disconnect the left half's USB data cable so the test
   exercises Bluetooth output. ZMK normally prefers USB while it is available.
2. From typing mode, hold the middle left thumb (SYM+NUM) and tap Q to select
   Bluetooth profile 0, or W for profile 1. Pair **Zen R4 Split** in the computer's
   Bluetooth settings. The right half remains paired to the left half.
3. Type letters, Shift combinations, Space and Backspace using both halves.
4. Tap the right middle thumb for onboard Javelin. With Plover closed, test KAT,
   repeat and star undo. Select the appropriate saved host layout over USB before
   moving between Mac and Windows if needed.
5. Return to typing with the outer left thumb. Connect Plover to the keyboard,
   enter Plover HID with the outer right thumb, and test KAT and multi-key chords
   using both halves. Record whether Plover can discover the Bluetooth device.
6. Power-cycle both halves and verify reconnect and all three modes again.

SYM+NUM + R clears the selected host profile's Bluetooth bond. Use it only if
repairing a failed host pairing; remove the corresponding computer-side pairing
before pairing again. It is not needed for a normal first test.

Firmware behavior is unchanged on this branch. Battery life and flash power
management are separate follow-up work: the current left configuration disables
sleep and keeps the external dictionary flash powered.
