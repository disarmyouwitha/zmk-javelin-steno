#include "battery_log.h"
#include "lapwing_flash.h"

#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/init.h>
#ifdef CONFIG_LOG_BACKEND_UART
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#endif
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zmk/activity.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>

#define SAMPLE_SECONDS 300
#define SECTOR_BYTES 4096
#define SECTOR_COUNT (FIXED_PARTITION_SIZE(battery_log_partition) / SECTOR_BYTES)

enum sample_reason { SAMPLE_BOOT, SAMPLE_PERIODIC, SAMPLE_USB, SAMPLE_SLEEP };
enum sample_flag {
    HOST_BLE = BIT(0), FLASH_POWERED = BIT(1), QSPI_MAPPED = BIT(2), SPLIT_BLE = BIT(3),
};

struct sample {
    uint32_t sequence;
    uint32_t boot;
    uint32_t uptime_ms;
    uint32_t left_presses;
    uint32_t right_presses;
    uint32_t usb_editor_commands;
    uint32_t ble_editor_commands;
    uint32_t reset_cause;
    int16_t battery_mv;
    int8_t battery_pct;
    uint8_t reason;
    uint8_t activity;
    uint8_t usb;
    uint8_t output;
    uint8_t layer;
    uint8_t flags;
    uint8_t reserved[3];
};

BUILD_ASSERT(sizeof(struct sample) == 44);
BUILD_ASSERT(FIXED_PARTITION_SIZE(battery_log_partition) % SECTOR_BYTES == 0);

static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
static struct flash_sector sectors[SECTOR_COUNT];
static struct fcb log_buffer = {
    .f_magic = 0x314c425a, .f_version = 1, .f_sectors = sectors,
};
K_MUTEX_DEFINE(log_lock);
static atomic_t ready;
static atomic_t left_presses, right_presses, usb_editor_commands, ble_editor_commands;
static uint32_t sequence, boot, reset_cause;
static bool first_sample = true;

static int read_sample(struct fcb_entry_ctx *entry, struct sample *sample) {
    if (entry->loc.fe_data_len != sizeof(*sample)) {
        return -EINVAL;
    }
    return flash_area_read(entry->fap, FCB_ENTRY_FA_DATA_OFF(entry->loc), sample, sizeof(*sample));
}

static int find_latest(struct fcb_entry_ctx *entry, void *context) {
    return read_sample(entry, context);
}

static int append_sample(const struct sample *sample) {
    struct fcb_entry entry;
    int rc = fcb_append(&log_buffer, sizeof(*sample), &entry);
    if (rc == -ENOSPC) {
        rc = fcb_rotate(&log_buffer);
        if (rc == 0) {
            rc = fcb_append(&log_buffer, sizeof(*sample), &entry);
        }
    }
    if (rc == 0) {
        rc = flash_area_write(log_buffer.fap, FCB_ENTRY_FA_DATA_OFF(entry), sample, sizeof(*sample));
    }
    if (rc == 0) {
        rc = fcb_append_finish(&log_buffer, &entry);
    }
    return rc;
}

static void check_split_link(struct bt_conn *conn, void *context) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0 && info.state == BT_CONN_STATE_CONNECTED &&
        info.role == BT_CONN_ROLE_CENTRAL) {
        *(bool *)context = true;
    }
}

/* All writes run on the system work queue, which also owns ZMK's sleep
 * transition. The USB reader runs separately and can briefly defer sampling.
 */
static void record_sample(enum sample_reason reason) {
    if (!atomic_get(&ready) || k_mutex_lock(&log_lock, K_NO_WAIT) != 0) {
        return;
    }
    bool split_connected = false;
    bt_conn_foreach(BT_CONN_TYPE_LE, check_split_link, &split_connected);
    struct sample sample = {
        .sequence = sequence + 1,
        .boot = boot,
        .uptime_ms = (uint32_t)k_uptime_get(),
        .left_presses = atomic_get(&left_presses),
        .right_presses = atomic_get(&right_presses),
        .usb_editor_commands = atomic_get(&usb_editor_commands),
        .ble_editor_commands = atomic_get(&ble_editor_commands),
        .reset_cause = reset_cause,
        .battery_mv = -1,
        .battery_pct = -1,
        .reason = reason,
        .activity = zmk_activity_get_state(),
        .usb = zmk_usb_get_conn_state(),
        .output = zmk_endpoint_get_selected().transport,
        .layer = zmk_keymap_highest_layer_active(),
        .flags = (zmk_ble_active_profile_is_connected() ? HOST_BLE : 0) |
                 (lapwing_flash_is_powered() ? FLASH_POWERED : 0) |
                 (lapwing_mapped_address() ? QSPI_MAPPED : 0) |
                 (split_connected ? SPLIT_BLE : 0),
    };
    struct sensor_value voltage, charge;
    if (device_is_ready(battery) && sensor_sample_fetch(battery) == 0 &&
        sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage) == 0 &&
        sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &charge) == 0) {
        sample.battery_mv = voltage.val1 * 1000 + voltage.val2 / 1000;
        sample.battery_pct = charge.val1;
    }
    const int rc = append_sample(&sample);
    if (rc == 0) {
        sequence = sample.sequence;
    } else {
        printk("Battery log append failed: %d\n", rc);
    }
    k_mutex_unlock(&log_lock);
}

static void sample_periodically(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(periodic_work, sample_periodically);

static void sample_periodically(struct k_work *work) {
    ARG_UNUSED(work);
    record_sample(first_sample ? SAMPLE_BOOT : SAMPLE_PERIODIC);
    first_sample = false;
    k_work_reschedule(&periodic_work, K_SECONDS(SAMPLE_SECONDS));
}

static void sample_usb(struct k_work *work) {
    ARG_UNUSED(work);
    record_sample(SAMPLE_USB);
}
K_WORK_DELAYABLE_DEFINE(usb_work, sample_usb);

static int note_activity(const zmk_event_t *event) {
    const struct zmk_position_state_changed *key = as_zmk_position_state_changed(event);
    if (key && key->state) {
        atomic_inc(key->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL ?
                   &left_presses : &right_presses);
    }
    if (as_zmk_usb_conn_state_changed(event) && atomic_get(&ready)) {
        k_work_reschedule(&usb_work, K_MSEC(250));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zen_battery_log, note_activity);
ZMK_SUBSCRIPTION(zen_battery_log, zmk_position_state_changed);
ZMK_SUBSCRIPTION(zen_battery_log, zmk_usb_conn_state_changed);

void zen_battery_log_editor_command(bool bluetooth) {
    atomic_inc(bluetooth ? &ble_editor_commands : &usb_editor_commands);
}

void zen_battery_log_sleep(void) {
    record_sample(SAMPLE_SLEEP);
}

static void write_serial(const struct device *console, const char *line) {
    while (*line) {
        uart_poll_out(console, *line++);
    }
}

struct export_context {
    const struct device *console;
    uint32_t count;
};

static int export_sample(struct fcb_entry_ctx *entry, void *arg) {
    struct export_context *context = arg;
    uint32_t dtr;
    if (uart_line_ctrl_get(context->console, UART_LINE_CTRL_DTR, &dtr) != 0 || !dtr) {
        return -ENOTCONN;
    }
    struct sample sample;
    const int rc = read_sample(entry, &sample);
    if (rc != 0) {
        return rc;
    }
    char row[176];
    const int length = snprintk(row, sizeof(row),
        "%u,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
        sample.sequence, sample.boot, sample.uptime_ms, sample.battery_mv, sample.battery_pct,
        sample.reason, sample.activity, sample.usb, sample.output, sample.layer, sample.flags,
        sample.left_presses, sample.right_presses, sample.usb_editor_commands,
        sample.ble_editor_commands, sample.reset_cause);
    if (length < 0 || length >= sizeof(row)) {
        return -EOVERFLOW;
    }
    char prefix[24];
    snprintk(prefix, sizeof(prefix), "ZBL ROW %08x ", crc32_ieee(row, length));
    write_serial(context->console, prefix);
    write_serial(context->console, row);
    write_serial(context->console, "\r\n");
    context->count++;
    /* Give the CDC transmit ring time to drain on large exports. */
    k_msleep(4);
    return 0;
}

void zen_battery_log_export(const struct device *console) {
    if (!atomic_get(&ready)) {
        write_serial(console, "\r\nZBL ERROR unavailable\r\n");
        return;
    }
    k_mutex_lock(&log_lock, K_FOREVER);
#ifdef CONFIG_LOG_BACKEND_UART
    // Diagnostics share this CDC port. Pause their backend while framing rows.
    const struct log_backend *backend = log_backend_get_by_name("log_backend_uart");
    const bool restore_logging = backend && log_backend_is_active(backend);
    if (restore_logging) {
        log_backend_deactivate(backend);
    }
#endif
    char line[96];
    snprintk(line, sizeof(line), "\r\nZBL BEGIN 1 %u %u %u\r\n",
             boot, (uint32_t)k_uptime_get(), SAMPLE_SECONDS);
    write_serial(console, line);
    struct export_context context = { .console = console };
    const int rc = fcb_walk(&log_buffer, NULL, export_sample, &context);
    snprintk(line, sizeof(line), "ZBL END %u %d\r\n", context.count, rc);
    write_serial(console, line);
#ifdef CONFIG_LOG_BACKEND_UART
    if (restore_logging) {
        log_backend_activate(backend, backend->cb->ctx);
    }
#endif
    k_mutex_unlock(&log_lock);
}

static int initialize_log(void) {
    uint32_t sector_count = ARRAY_SIZE(sectors);
    const int area = FIXED_PARTITION_ID(battery_log_partition);
    int rc = flash_area_get_sectors(area, &sector_count, sectors);
    if (rc == 0 && sector_count == ARRAY_SIZE(sectors)) {
        log_buffer.f_sector_cnt = sector_count;
        rc = fcb_init(area, &log_buffer);
    } else if (rc == 0) {
        rc = -EINVAL;
    }
    struct sample latest = {0};
    if (rc == 0) {
        rc = fcb_walk(&log_buffer, NULL, find_latest, &latest);
    }
    if (rc != 0) {
        printk("Battery log unavailable: %d\n", rc);
        return 0;
    }
    sequence = latest.sequence;
    boot = latest.boot + 1;
    if (hwinfo_get_reset_cause(&reset_cause) == 0) {
        hwinfo_clear_reset_cause();
    }
    atomic_set(&ready, 1);
    k_work_schedule(&periodic_work, K_SECONDS(2));
    return 0;
}

SYS_INIT(initialize_log, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
