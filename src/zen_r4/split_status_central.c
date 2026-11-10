#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/service.h>
#include <zmk/split/central.h>

#include "split_status.h"

BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT == 1);
BUILD_ASSERT(sizeof(ZEN_R4_STATUS_BEHAVIOR) <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN);

static atomic_t connected;
static atomic_t sync_attempts;

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
        return ZEN_R4_MODE_JAVELIN;
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
    // Send a few snapshots during discovery, then only send on layer changes.
    if (atomic_get(&connected) && atomic_dec(&sync_attempts) > 1) {
        k_work_reschedule(k_work_delayable_from_work(work), K_SECONDS(1));
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
        atomic_set(&sync_attempts, 4);
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

static int layer_changed(const zmk_event_t *event) {
#if IS_ENABLED(CONFIG_ZMK_ZEN_R4_PRACTICE)
    const char *name =
        zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()));
    zen_r4_practice_set_active(name && strcmp(name, "PRACTICE") == 0);
#endif
    zen_r4_split_status_refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zen_r4_split_status, layer_changed);
ZMK_SUBSCRIPTION(zen_r4_split_status, zmk_layer_state_changed);
