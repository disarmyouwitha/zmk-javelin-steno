#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zmk/ble.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* The dynamic bt/name setting survives flashing and clearing a host profile.
 * Migrate only our retired name; leave names chosen by the user alone.
 */
static int migrate_bluetooth_name(void) {
    const char *name = bt_get_name();
    if (!name || strcmp(name, "Zen R4 Split") != 0) {
        return 0;
    }

    char replacement[] = CONFIG_BT_DEVICE_NAME;
    const int err = zmk_ble_set_device_name(replacement);
    if (err) {
        LOG_WRN("Could not migrate Bluetooth device name (%d)", err);
        return err;
    }
    LOG_INF("Migrated Bluetooth device name to %s", replacement);
    return 0;
}

/* Bluetooth loads its saved name and ZMK starts advertising at commit priority
 * zero. Run afterwards so the saved name cannot overwrite the migration and
 * the ZMK setter can refresh advertising too. No additional marker is needed:
 * subsequent boots see the new name and do nothing.
 */
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(steno_squid_name, "steno_squid/name", NULL, NULL,
                                         migrate_bluetooth_name, NULL, 10);
