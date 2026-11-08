# Hybrid split Bluetooth battery test

Branch: `zen-r4/bluetooth-battery-test`, starting from `main` at `c2ba6d4`.

## What is implemented

| Feature | Firmware status | Battery-only hardware test |
| --- | --- | --- |
| Right half to left half | Existing ZMK Bluetooth split connection | Previously passed; repeat with left on battery |
| Normal typing to computer | ZMK BLE keyboard output enabled | Pending |
| Onboard Javelin to computer | Translation uses ZMK keycode events and its selected output | Pending |
| Plover HID to computer | Module implements BLE HID over GATT reports and endpoint routing | Pending; host/Plover device discovery also needs verification |
| Javelin browser dictionary tools | Custom console is currently registered only as USB `HID_2` | Wireless transport not implemented |

The browser tools have a separate “Select Bluetooth device...” option. Supporting
that requires checking its expected GATT protocol and implementing a compatible
firmware console transport. Existing Plover HID over GATT does not carry editor
commands. No hosted website change has been established as necessary.

## Pair and test

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
