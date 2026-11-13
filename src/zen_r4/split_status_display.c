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
    if (binding->param2 == ZEN_R4_PRACTICE_PROTOCOL) {
        uint32_t value = binding->param1;
        unsigned round = (value >> 4) & 31;
        unsigned feedback = (value >> 2) & 3;
        unsigned drill = (value & ZEN_R4_PRACTICE_DRILL_MASK) >> ZEN_R4_PRACTICE_DRILL_SHIFT;
        unsigned total = zen_r4_practice_rounds(drill);
        if (!(value & ZEN_R4_PRACTICE_ACTIVE) ||
            (value & ~(ZEN_R4_PRACTICE_ACTIVE | ZEN_R4_PRACTICE_NO_DICTIONARY |
                       ZEN_R4_PRACTICE_DRILL_MASK | 0x7ffffu)) ||
            (value & ZEN_R4_PRACTICE_DRILL_MASK) >=
                (ZEN_R4_PRACTICE_DRILL_COUNT << ZEN_R4_PRACTICE_DRILL_SHIFT) ||
            round > total ||
            ((round == total) != (feedback == ZEN_R4_PRACTICE_DONE)) ||
            ((value >> 9) & 31) > total ||
            ((value >> 14) & 31) > total) {
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

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = zen_r4_screen_create();
    refresh_status(NULL);
    return screen;
}
