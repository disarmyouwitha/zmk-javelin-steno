#define DT_DRV_COMPAT zmk_behavior_zen_r4_status

#include <drivers/behavior.h>
#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

#include "display/screen.h"
#include "display/pet_widget.h"
#include "split_status.h"

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT));
BUILD_ASSERT(__builtin_strcmp(DEVICE_DT_NAME(DT_DRV_INST(0)), ZEN_R4_STATUS_BEHAVIOR) == 0);

/* Past the modes the left can send: show the layer name it sent instead. */
#define MODE_LAYER_NAME ZEN_R4_MODE_COUNT

static atomic_t mode;
static atomic_t layer_name_low;
static atomic_t layer_name_high;
static atomic_t practice_screen;
static atomic_t pet_wake;
static atomic_t pet_typing;

static void refresh_status(struct k_work *work) {
    const unsigned shown = atomic_get(&mode);
    char name[ZEN_R4_LAYER_NAME_CHARS + 1] = {0};
    if (shown == MODE_LAYER_NAME) {
        const uint64_t packed = (uint64_t)(uint32_t)atomic_get(&layer_name_high) << 32 |
                                (uint32_t)atomic_get(&layer_name_low);
        for (int i = 0; i < ZEN_R4_LAYER_NAME_CHARS; i++) {
            name[i] = (packed >> (7 * i)) & 0x7f;
        }
    }
    const uint32_t practice = atomic_get(&practice_screen);
    const bool wake = atomic_clear(&pet_wake);
    const bool typing = atomic_clear(&pet_typing);
    const bool practicing = (practice & ZEN_R4_PRACTICE_ACTIVE) != 0;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    zen_r4_pet_set_powered(zmk_usb_is_powered());
#endif
    if (wake && !practicing) {
        zen_r4_pet_set_state(PET_IDLE);
    }
    zen_r4_screen_update(shown, name, practice, zmk_battery_state_of_charge());
    if (!practicing) {
        if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
            zen_r4_pet_set_state(PET_SLEEP);
        } else if (typing) {
            zen_r4_pet_set_state(PET_TYPING);
        }
    }
}

K_WORK_DEFINE(refresh_status_work, refresh_status);

static void queue_refresh(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &refresh_status_work);
    }
}

static int status_received(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    if (binding->param2 == ZEN_R4_PRACTICE_PROTOCOL ||
        binding->param2 == ZEN_R4_PRACTICE_LEGACY_PROTOCOL) {
        const uint32_t raw = binding->param1;
        const bool legacy = binding->param2 == ZEN_R4_PRACTICE_LEGACY_PROTOCOL;
        const unsigned drill = (raw >> (legacy ? 20 : ZEN_R4_PRACTICE_DRILL_SHIFT)) & 3u;
        const uint32_t allowed = ZEN_R4_PRACTICE_ACTIVE | (legacy ? 0x3fffffu : 0x7ffffffu);
        if (!(raw & ZEN_R4_PRACTICE_ACTIVE) || (raw & ~allowed) ||
            drill >= ZEN_R4_PRACTICE_DRILL_COUNT) {
            return -EINVAL;
        }
        uint32_t value = raw;
        if (legacy) {
            const unsigned round = (raw >> 4) & 31;
            const unsigned prompt = drill == ZEN_R4_PRACTICE_WORDS ? raw & 3u :
                                    round % zen_r4_practice_prompt_count(drill);
            value = ZEN_R4_PRACTICE_ACTIVE | ZEN_R4_PRACTICE_LEGACY | prompt |
                    (((raw >> 2) & 3u) << ZEN_R4_PRACTICE_FEEDBACK_SHIFT) |
                    (round << ZEN_R4_PRACTICE_ROUND_SHIFT) |
                    (((raw >> 9) & 31u) << ZEN_R4_PRACTICE_STREAK_SHIFT) |
                    (((raw >> 14) & 31u) << ZEN_R4_PRACTICE_BEST_SHIFT) |
                    (drill << ZEN_R4_PRACTICE_DRILL_SHIFT) |
                    ((raw & (1u << 19)) ? ZEN_R4_PRACTICE_NO_DICTIONARY : 0);
        }
        const unsigned total = legacy && drill != ZEN_R4_PRACTICE_WORDS ? 30 :
                               zen_r4_practice_rounds(drill);
        const unsigned round = zen_r4_practice_counter(value, ZEN_R4_PRACTICE_ROUND_SHIFT);
        if (round > total ||
            ((round == total) != (zen_r4_practice_feedback_value(value) == ZEN_R4_PRACTICE_DONE)) ||
            zen_r4_practice_counter(value, ZEN_R4_PRACTICE_STREAK_SHIFT) > total ||
            zen_r4_practice_counter(value, ZEN_R4_PRACTICE_BEST_SHIFT) > total ||
            (value & ZEN_R4_PRACTICE_PROMPT_MASK) >= zen_r4_practice_prompt_count(drill)) {
            return -EINVAL;
        }
        atomic_set(&practice_screen, value);
    } else if (binding->param2 & ZEN_R4_LAYER_NAME_PROTOCOL) {
        atomic_clear(&practice_screen);
        atomic_set(&layer_name_low, binding->param1);
        atomic_set(&layer_name_high, binding->param2 & ~ZEN_R4_LAYER_NAME_PROTOCOL);
        atomic_set(&mode, MODE_LAYER_NAME);
    } else if (binding->param2 == ZEN_R4_STATUS_PROTOCOL &&
               binding->param1 < ZEN_R4_MODE_COUNT) {
        atomic_clear(&practice_screen);
        atomic_set(&mode, binding->param1);
    } else {
        return -EINVAL;
    }
    queue_refresh();
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api status_driver = {
    .binding_pressed = status_received,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &status_driver);

static int display_event(const zmk_event_t *event) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(event);
    if (position) {
        if (position->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL && position->state) {
            atomic_set(&pet_typing, 1);
            queue_refresh();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(event);
    if (activity && activity->state == ZMK_ACTIVITY_ACTIVE) {
        atomic_set(&pet_wake, 1);
    }
    const struct zmk_split_peripheral_status_changed *split =
        as_zmk_split_peripheral_status_changed(event);
    if (split && !split->connected) {
        atomic_clear(&mode);
        atomic_clear(&practice_screen);
    }
    queue_refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zen_r4_display, display_event);
ZMK_SUBSCRIPTION(zen_r4_display, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(zen_r4_display, zmk_split_peripheral_status_changed);
ZMK_SUBSCRIPTION(zen_r4_display, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(zen_r4_display, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(zen_r4_display, zmk_usb_conn_state_changed);
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = zen_r4_screen_create();
    refresh_status(NULL);
    return screen;
}
