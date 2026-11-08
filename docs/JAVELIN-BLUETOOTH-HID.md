# Javelin editor over Bluetooth HID

Branch: `zen-r4/javelin-bluetooth-hid`. Firmware builds and native transport tests
pass. On the flashed left half, macOS exposed the editor collection over both
USB and Bluetooth. A native HID client completed the Javelin handshake and read
the dictionary list on both transports. Over Bluetooth it also added, replaced,
read back, and removed a temporary personal entry. With USB unplugged, the
existing site connected through **Connect via HID** and loaded **Active
Dictionaries**.

Disabling every dictionary exposed an empty-history bug in the pinned engine.
With a maximum outline length of zero, the first steno stroke could ask for the
last entry of an empty history. The firmware builds a private engine copy with
an explicit empty-history check. A native regression covers raw steno output
and undo with all dictionaries disabled.

## Compatibility requirement

Use the existing Javelin Steno Tools website, unchanged, through **Connect via
HID**. Do not require its separate Bluetooth picker or a modified website.

The existing client filters for usage page `0xFF31`, usage `0x74`, and calls
`sendReport(0, data)`. The firmware advertises a separate HID-over-GATT service
with that collection, 63-byte input/output reports, no report ID in the report
map, and report reference ID 0. USB keeps its existing descriptor and interface.

The BLE connection must be encrypted and belong to the selected ZMK host
profile. Replies go only to the connection/session that submitted the request.
Disconnected sessions cannot execute their queued commands. USB and Bluetooth
reassembly buffers distinguish their transport and session even if both browser
connections use the same console channel number.

The worker dispatches the same restricted editor commands to the same locked
Javelin engine and personal dictionary store. Bluetooth callbacks only enqueue
input; flash writes and engine commands run on the console worker. ZMK typing,
onboard translation, and Plover HID keep their existing transports.

63-byte notifications require an ATT MTU of at least 66. The left profile sets
the L2CAP TX MTU to 80 and ACL RX size to 84. Hosts that negotiate a smaller MTU
cannot use this console. The send path does not truncate replies to fit.

## Hardware verification

1. Flash the left half, preserving settings and both external base dictionaries.
2. Confirm USB HID editor handshake still works with the unmodified website.
3. Pair/connect **Zen R4 Split** in macOS Bluetooth settings. Unplug USB.
4. Open the existing website in Chrome, choose **Connect via HID**, and check
   whether its filtered picker offers the Bluetooth editor collection.
5. Verify handshake, dictionary list/checkboxes, and host layout selection.
6. Add a uniquely named temporary personal entry, replace it, and verify lookup.
   Reboot, reconnect through HID, verify persistence, then remove that entry.
7. Confirm typing, onboard Javelin, and Plover HID still work on both halves.
8. Repeat after reconnecting and after a full power cycle. Check Windows support
   separately; macOS success does not establish compatibility with every host.

macOS may cache the old GATT services. If the new editor collection does not
appear, investigate the cached HID/GATT enumeration before concluding WebHID
cannot access it. Clearing a host bond is a separate troubleshooting action;
do not reset all settings or erase dictionary flash.

## Native regression check

Run `python3 tools/tests/test_javelin_hid_transport.py`. It compiles the actual
console worker against native USB/BLE driver doubles and checks Bluetooth with
USB disconnected, identical channel numbers across transports, stale-session
rejection, the command allowlist, oversized commands, and USB reconnection.
This does not emulate the host Bluetooth HID driver or establish browser support.
