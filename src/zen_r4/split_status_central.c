#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/service.h>
#include <zmk/split/central.h>

#include "split_status.h"
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
#include "lapwing_flash.h"
#endif

BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT == 1);
BUILD_ASSERT(sizeof(ZEN_R4_STATUS_BEHAVIOR) <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN);

/* Four snapshots a second apart, then six more spread over half a minute. */
#define SYNC_FAST_ATTEMPTS 4
#define SYNC_SLOW_ATTEMPTS 6

static atomic_t connected;
static atomic_t sync_attempts;

/* A layer that needs the dictionaries says so while they are not usable. */
static enum zen_r4_mode dictionary_mode(enum zen_r4_mode ready) {
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
    switch (lapwing_flash_status()) {
    case LAPWING_FLASH_LOADING:
        return ZEN_R4_MODE_LOADING;
    case LAPWING_FLASH_UNAVAILABLE:
        return ZEN_R4_MODE_NO_DICTIONARY;
    default:
        break;
    }
#endif
    return ready;
}

static enum zen_r4_mode current_mode(void) {
    const char *name =
        zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()));
    if (!name) {
        return ZEN_R4_MODE_UNKNOWN;
    }
    if (strcmp(name, "MAIN") == 0) {
        return ZEN_R4_MODE_TYPING;
    }
    if (strcmp(name, "SYM+NUM") == 0) {
        return ZEN_R4_MODE_SYMBOLS;
    }
    if (strcmp(name, "STENO") == 0) {
        return dictionary_mode(ZEN_R4_MODE_JAVELIN);
    }
    if (strcmp(name, "PRACTICE") == 0) {
        /* Shown only until practice starts and sends its own screen. */
        return dictionary_mode(ZEN_R4_MODE_UNKNOWN);
    }
    if (strcmp(name, "PLOVER HID") == 0) {
        return ZEN_R4_MODE_PLOVER;
    }
    return ZEN_R4_MODE_UNKNOWN;
}

static void send_status(struct k_work *work) {
    if (!atomic_get(&connected)) {
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = ZEN_R4_STATUS_BEHAVIOR,
        .param1 = current_mode(),
        .param2 = ZEN_R4_STATUS_PROTOCOL,
    };
#if IS_ENABLED(CONFIG_ZMK_ZEN_R4_PRACTICE)
    uint32_t practice = zen_r4_practice_snapshot();
    if (practice & ZEN_R4_PRACTICE_ACTIVE) {
        binding.param1 = practice;
        binding.param2 = ZEN_R4_PRACTICE_PROTOCOL;
    }
#endif
    struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};
    zmk_split_central_invoke_behavior(0, &binding, event, true);
}

K_WORK_DEFINE(send_status_work, send_status);

void zen_r4_split_status_refresh(void) {
    k_work_submit(&send_status_work);
}

static void sync_after_connect(struct k_work *work) {
    send_status(work);
    // ZMK discovers the remote behavior characteristic after the link comes up.
    // Send snapshots through discovery, which can outlast the first few seconds
    // on a fresh pairing. After that, only send on layer changes.
    const atomic_val_t remaining = atomic_dec(&sync_attempts) - 1;
    if (atomic_get(&connected) && remaining > 0) {
        k_work_reschedule(k_work_delayable_from_work(work),
                          remaining > SYNC_SLOW_ATTEMPTS ? K_SECONDS(1) : K_SECONDS(5));
    }
}

K_WORK_DELAYABLE_DEFINE(sync_status_work, sync_after_connect);

static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

static void status_connected(struct bt_conn *conn, uint8_t err) {
    if (err == 0 && is_split_link(conn)) {
        atomic_set(&connected, 1);
        atomic_set(&sync_attempts, SYNC_FAST_ATTEMPTS + SYNC_SLOW_ATTEMPTS);
        k_work_reschedule(&sync_status_work, K_MSEC(500));
    }
}

static void status_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (is_split_link(conn)) {
        atomic_clear(&connected);
        k_work_cancel_delayable(&sync_status_work);
    }
}

BT_CONN_CB_DEFINE(zen_r4_split_status_conn) = {
    .connected = status_connected,
    .disconnected = status_disconnected,
};

static void apply_layer(void) {
    const char *name =
        zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()));
    const bool practice = name && strcmp(name, "PRACTICE") == 0;
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
    /* Have the dictionaries ready before the first stroke on these layers. */
    if (practice || (name && strcmp(name, "STENO") == 0)) {
        lapwing_flash_prepare();
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_ZEN_R4_PRACTICE)
    zen_r4_practice_set_active(practice);
#else
    ARG_UNUSED(practice);
#endif
    zen_r4_split_status_refresh();
}

static int layer_changed(const zmk_event_t *event) {
    apply_layer();
    return ZMK_EV_EVENT_BUBBLE;
}

#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
/* The dictionaries became usable, or a check gave up. Practice selected during
 * the check starts now, and the right display drops its loading message.
 */
static void flash_ready(struct k_work *work) {
    apply_layer();
}

K_WORK_DEFINE(flash_ready_work, flash_ready);

void lapwing_flash_ready_changed(void) {
    k_work_submit(&flash_ready_work);
}
#endif

ZMK_LISTENER(zen_r4_split_status, layer_changed);
ZMK_SUBSCRIPTION(zen_r4_split_status, zmk_layer_state_changed);
