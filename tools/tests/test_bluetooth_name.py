#!/usr/bin/env python3
"""Exercise the real name migration callback with Bluetooth API doubles."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

STUBS = r'''
#pragma once
#define CONFIG_ZMK_LOG_LEVEL 3
#define CONFIG_BT_DEVICE_NAME "Steno Squid"
#define LOG_MODULE_DECLARE(...)
#define LOG_WRN(...)
#define LOG_INF(...)
const char *bt_get_name(void);
int zmk_ble_set_device_name(char *name);
#define SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(id, tree, get, set, commit, export, priority) \
    static int (*registered_commit)(void) = commit; \
    static const int commit_priority = priority
'''
HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bluetooth_name.c"
static const char *current;
static char saved[32];
static int calls, fail;
const char *bt_get_name(void) { return current; }
int zmk_ble_set_device_name(char *name) {
    calls++;
    if (fail) return fail;
    snprintf(saved, sizeof(saved), "%s", name);
    current = saved;
    return 0;
}
int main(void) {
    // The real Zephyr settings loader commits Bluetooth at priority zero.
    assert(commit_priority > 0);
    current = "Zen R4 Split";
    assert(registered_commit() == 0);
    assert(calls == 1 && !strcmp(current, CONFIG_BT_DEVICE_NAME));
    assert(registered_commit() == 0 && calls == 1); // Later boot: no new write.
    current = "My Squid";
    assert(registered_commit() == 0 && calls == 1);
    assert(!strcmp(current, "My Squid"));
    current = CONFIG_BT_DEVICE_NAME;
    assert(registered_commit() == 0 && calls == 1);
    current = NULL;
    assert(registered_commit() == 0 && calls == 1);
    current = "Zen R4 Split";
    fail = -EIO;
    assert(registered_commit() == -EIO && calls == 2);
    assert(!strcmp(current, "Zen R4 Split"));
    fail = 0;
    assert(registered_commit() == 0 && calls == 3); // Retry succeeds.
    assert(!strcmp(current, CONFIG_BT_DEVICE_NAME));
    puts("Bluetooth name migration: legacy, repeat boot, custom name and failure checks passed");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='steno-squid-name-') as directory:
        output = Path(directory)
        (output / 'stubs.h').write_text(STUBS)
        for name in ('zephyr/bluetooth/bluetooth.h', 'zephyr/logging/log.h',
                     'zephyr/settings/settings.h', 'zmk/ble.h'):
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "stubs.h"\n')
        (output / 'test.c').write_text(HARNESS)
        binary = output / 'test'
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(output), '-I' + str(ROOT / 'src/steno_squid'),
                        str(output / 'test.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
