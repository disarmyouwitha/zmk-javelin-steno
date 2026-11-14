#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/service.h>
#include <zmk/split/central.h>
#include <dt-bindings/zmk/javelin_steno_layers.h>

#include "split_status.h"
#include "stroke_feed.h"
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
#include "lapwing_flash.h"
#endif

BUILD_ASSERT(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT == 1);

/* Modes are recognised by layer ID, a layer's position in the devicetree
 * keymap. ZMK Studio can rename or reorder layers without changing it. These
 * checks fail the build if the keymap order drifts from javelin_steno_layers.h.
 */
#define CHECK_LAYER_ID(node, id)                                                                   \
    COND_CODE_1(DT_NODE_EXISTS(node), (BUILD_ASSERT(DT_NODE_CHILD_IDX(node) == (id));), ())
CHECK_LAYER_ID(DT_PATH(keymap, main_layer), JAVELIN_LAYER_MAIN)
CHECK_LAYER_ID(DT_PATH(keymap, sym_num_layer), JAVELIN_LAYER_SYM_NUM)
CHECK_LAYER_ID(DT_PATH(keymap, steno_layer), JAVELIN_LAYER_STENO)
CHECK_LAYER_ID(DT_PATH(keymap, plover_hid_layer), JAVELIN_LAYER_PLOVER_HID)

/* The names the keymap gives its layers, by layer ID. A layer that still has
 * its keymap name shows the firmware's own label for its mode.
 */
#define KEYMAP_LAYER_NAME(node) DT_PROP_OR(node, display_name, DT_PROP_OR(node, label, ""))
static const char *const keymap_layer_names[] = {
    ZMK_KEYMAP_LAYERS_FOREACH_SEP(KEYMAP_LAYER_NAME, (, ))};

#if IS_ENABLED(CONFIG_ZMK_STENO_SQUID_PRACTICE)
/* Practice runs on the steno layer while this is set. */
static atomic_t practice_wanted;
#endif

static zmk_keymap_layer_id_t active_layer(void) {
    return zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());
}
BUILD_ASSERT(sizeof(STENO_SQUID_STATUS_BEHAVIOR) <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN);

/* Four snapshots a second apart, then six more spread over half a minute.
 * After that one snapshot a minute, so a message the right half missed is
 * repaired within a minute. It is a single small write on a link that is
 * already open.
 */
#define SYNC_FAST_ATTEMPTS 4
#define SYNC_SLOW_ATTEMPTS 6
#define SYNC_HEARTBEAT_SECONDS 60

static atomic_t connected;
static atomic_t sync_attempts;

/* A layer that needs the dictionaries says so while they are not usable. */
static enum steno_squid_mode dictionary_mode(enum steno_squid_mode ready) {
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
    switch (lapwing_flash_status()) {
    case LAPWING_FLASH_LOADING:
        return STENO_SQUID_MODE_LOADING;
    case LAPWING_FLASH_UNAVAILABLE:
        return STENO_SQUID_MODE_NO_DICTIONARY;
    default:
        break;
    }
#endif
    return ready;
}

static enum steno_squid_mode current_mode(void) {
    switch (active_layer()) {
    case JAVELIN_LAYER_SYM_NUM:
        return STENO_SQUID_MODE_SYMBOLS;
    case JAVELIN_LAYER_STENO:
        return dictionary_mode(STENO_SQUID_MODE_JAVELIN);
    case JAVELIN_LAYER_PLOVER_HID:
        return STENO_SQUID_MODE_PLOVER;
    default:
        /* The main layer, and any layer a user adds in ZMK Studio. */
        return STENO_SQUID_MODE_TYPING;
    }
}

/* The active layer's name when the display should show it: the layer was
 * renamed in ZMK Studio, or it has no mode of its own. Otherwise NULL.
 */
static const char *custom_layer_name(void) {
    const zmk_keymap_layer_id_t layer = active_layer();
    const char *name = zmk_keymap_layer_name(layer);
    if (!name || !name[0]) {
        return NULL;
    }
    if (layer <= JAVELIN_LAYER_PLOVER_HID && layer < ARRAY_SIZE(keymap_layer_names) &&
        strcmp(name, keymap_layer_names[layer]) == 0) {
        return NULL;
    }
    return name;
}

static void pack_layer_name(const char *name, struct zmk_behavior_binding *binding) {
    uint64_t packed = 0;
    for (int i = 0; i < STENO_SQUID_LAYER_NAME_CHARS && name[i]; i++) {
        const uint8_t c = name[i];
        /* The display font has ASCII only. */
        packed |= (uint64_t)(c >= ' ' && c < 0x7f ? c : '?') << (7 * i);
    }
    binding->param1 = (uint32_t)packed;
    binding->param2 = (uint32_t)(packed >> 32) | STENO_SQUID_LAYER_NAME_PROTOCOL;
}

static void send_status(struct k_work *work) {
    if (!atomic_get(&connected)) {
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = STENO_SQUID_STATUS_BEHAVIOR,
        .param1 = current_mode(),
        .param2 = STENO_SQUID_STATUS_PROTOCOL,
    };
    /* Loading and No dictionary take the place of a layer's name. */
    const char *name = custom_layer_name();
    if (name && binding.param1 != STENO_SQUID_MODE_LOADING &&
        binding.param1 != STENO_SQUID_MODE_NO_DICTIONARY) {
        pack_layer_name(name, &binding);
    }
#if IS_ENABLED(CONFIG_ZMK_STENO_SQUID_PRACTICE)
    uint32_t practice = steno_squid_practice_snapshot();
    if (practice & STENO_SQUID_PRACTICE_ACTIVE) {
        binding.param1 = practice;
        binding.param2 = STENO_SQUID_PRACTICE_PROTOCOL;
    }
#endif
    struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};
    zmk_split_central_invoke_behavior(0, &binding, event, true);
}

K_WORK_DEFINE(send_status_work, send_status);

void steno_squid_split_status_refresh(void) {
    k_work_submit(&send_status_work);
}

#if IS_ENABLED(CONFIG_ZMK_STENO_SQUID_STROKE_TAPE)
/* The latest stroke waits here for the work queue. A stroke typed before the
 * last one was sent replaces it, so the display always gets the newest.
 */
static struct steno_squid_tape pending_tape;
static struct k_spinlock pending_tape_lock;

static void invoke_tape(uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = STENO_SQUID_STATUS_BEHAVIOR,
        .param1 = param1,
        .param2 = param2,
    };
    struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};
    zmk_split_central_invoke_behavior(0, &binding, event, true);
}

static void send_tape(struct k_work *work) {
    if (!atomic_get(&connected)) {
        return;
    }
    struct steno_squid_tape tape;
    k_spinlock_key_t key = k_spin_lock(&pending_tape_lock);
    tape = pending_tape;
    k_spin_unlock(&pending_tape_lock, key);
    if (!tape.valid) {
        return;
    }
    uint32_t param1, param2;
    steno_squid_tape_encode_stroke(&tape, &param1, &param2);
    invoke_tape(param1, param2);
    if (steno_squid_tape_needs_more(&tape)) {
        steno_squid_tape_encode_more(&tape, &param1, &param2);
        invoke_tape(param1, param2);
    }
    if (tape.hint_count) {
        steno_squid_tape_encode_hint(&tape, &param1, &param2);
        invoke_tape(param1, param2);
    }
}

K_WORK_DEFINE(send_tape_work, send_tape);

void steno_squid_tape_publish(const struct steno_squid_tape *tape) {
    k_spinlock_key_t key = k_spin_lock(&pending_tape_lock);
    pending_tape = *tape;
    k_spin_unlock(&pending_tape_lock, key);
    k_work_submit(&send_tape_work);
}
#endif

/* Report the active host profile, independent of the split Bluetooth link. */
static void send_host_status(struct k_work *work) {
    if (!atomic_get(&connected)) {
        return;
    }

    bool host_connected = zmk_ble_active_profile_is_connected();
#if IS_ENABLED(CONFIG_ZMK_USB)
    host_connected = host_connected || zmk_usb_is_hid_ready();
#endif
    struct zmk_behavior_binding binding = {
        .behavior_dev = STENO_SQUID_STATUS_BEHAVIOR,
        .param1 = !host_connected,
        .param2 = STENO_SQUID_HOST_STATUS_PROTOCOL,
    };
    struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};
    zmk_split_central_invoke_behavior(0, &binding, event, true);
}

K_WORK_DEFINE(send_host_status_work, send_host_status);

static int host_status_changed(const zmk_event_t *event) {
    k_work_submit(&send_host_status_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(steno_squid_host_status, host_status_changed);
ZMK_SUBSCRIPTION(steno_squid_host_status, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(steno_squid_host_status, zmk_usb_conn_state_changed);
#endif

static void sync_after_connect(struct k_work *work) {
    send_status(work);
    send_host_status(work);
    // ZMK discovers the remote behavior characteristic after the link comes up.
    // Send snapshots through discovery, which can outlast the first few seconds
    // on a fresh pairing. After that, only send on layer changes.
    if (!atomic_get(&connected)) {
        return;
    }
    k_timeout_t next = K_SECONDS(SYNC_HEARTBEAT_SECONDS);
    if (atomic_get(&sync_attempts) > 0) {
        const atomic_val_t remaining = atomic_dec(&sync_attempts) - 1;
        if (remaining > 0) {
            next = remaining > SYNC_SLOW_ATTEMPTS ? K_SECONDS(1) : K_SECONDS(5);
        }
    }
    k_work_reschedule(k_work_delayable_from_work(work), next);
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

BT_CONN_CB_DEFINE(steno_squid_split_status_conn) = {
    .connected = status_connected,
    .disconnected = status_disconnected,
};

static void apply_layer(void) {
    const bool steno = active_layer() == JAVELIN_LAYER_STENO;
    ARG_UNUSED(steno);
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
    /* Keep the dictionaries powered and ready while this layer is selected. */
    lapwing_flash_hold(steno);
#endif
#if IS_ENABLED(CONFIG_ZMK_STENO_SQUID_PRACTICE)
    /* Leaving the steno layer ends practice. */
    if (!steno) {
        atomic_clear(&practice_wanted);
    }
    const bool want_practice = steno && atomic_get(&practice_wanted);
#if IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING)
    /* Starting practice takes the engine lock, which waits for an idle flash
     * to power up. This runs on the thread that handles keys, so do not wait
     * here: lapwing_flash_hold() above has started the wake, and the ready
     * hook calls apply_layer() again once the dictionaries are mapped.
     */
    if (want_practice && !lapwing_mapped_address()) {
        steno_squid_split_status_refresh();
        return;
    }
#endif
    steno_squid_practice_set_active(want_practice);
#endif
    steno_squid_split_status_refresh();
}

#if IS_ENABLED(CONFIG_ZMK_STENO_SQUID_PRACTICE)
void steno_squid_practice_toggle(void) {
    if (active_layer() == JAVELIN_LAYER_STENO) {
        atomic_set(&practice_wanted, !atomic_get(&practice_wanted));
    } else {
        /* The layer change clears the flag on its way, so set it afterwards. */
        /* Match &to: clear locked layers as well. A locked Plover layer
         * otherwise stays above Javelin and prevents Practice from starting.
         */
        zmk_keymap_layer_to(JAVELIN_LAYER_STENO, true);
        atomic_set(&practice_wanted, 1);
    }
    apply_layer();
}
#endif

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

ZMK_LISTENER(steno_squid_split_status, layer_changed);
ZMK_SUBSCRIPTION(steno_squid_split_status, zmk_layer_state_changed);
