# Steno Squid battery logging trial

Battery logging is off by default. To enable it, use `-S zmk-usb-logging`
and add `-DCONFIG_ZMK_JAVELIN_BATTERY_LOG=y` to the split left build's CMake
arguments. Use a separate build directory for this variant. The left half
samples every five minutes, at startup, on USB state
changes, and after the flash rail is switched off just before deep sleep. The
128 KiB circular log uses a dedicated internal flash partition. At five-minute
intervals it holds roughly eight days of continuous awake samples, with extra
events reducing that window. Old sectors roll off as space is needed. The
standard split left build reserves the same flash partition so a later
logger build can export retained records.

Each row has the measured voltage and ZMK's estimated percent, a sequence number
and boot number, uptime, the event, activity, USB, host and split Bluetooth status,
selected output and layer, flash rail and mapping state, and the reset flags
reported by the controller. Cumulative keypress counts are split by half.
Editor command counts are split by USB and Bluetooth. The log does not contain
key identities, steno outlines, translations, or editor command contents.
Flash rail state records the firmware's GPIO setting. It does not measure
electrical current.

## Export after a run

Reconnect the left half to USB and run this from the repository with a Python
environment that has `pyserial` installed:

```sh
python3 tools/steno-squid-battery/export.py sleep-flash-off.csv \
  --label "Deep sleep with flash rail off" \
  --notes "Record battery age, LED state, and approximate sleep and wake times here"
```

Add `--battery-mah` with the nominal capacity if known. The command saves a CSV
and a JSON file containing the export time and experiment metadata. Each export
includes all retained samples. Use a new filename for each export and use
`sequence` to identify overlapping rows from the same device log. Export
before switching firmware variants so you can use the last sequence number
as the boundary between runs.

Uptime resets on wake from deep sleep. There are no samples while the controller
is off, and the duration between a sleep row and the next boot requires an
external timestamp. The CSV estimates UTC for the boot active at export. Earlier
boots retain their relative times. Reset flags are best effort because the
bootloader can clear them.

Compare battery-only rows with the same battery and similar workloads. Voltage
and percentage are useful trends, while current measurements are needed to
isolate power draw. The sampling and flash writes themselves add some overhead,
so keep this logger enabled in both builds when comparing firmware variants.
