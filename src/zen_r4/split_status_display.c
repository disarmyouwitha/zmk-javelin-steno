#define DT_DRV_COMPAT zmk_behavior_zen_r4_status

#include <drivers/behavior.h>
#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include "split_status.h"

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT));
BUILD_ASSERT(__builtin_strcmp(DEVICE_DT_NAME(DT_DRV_INST(0)), ZEN_R4_STATUS_BEHAVIOR) == 0);

static atomic_t mode;
static atomic_t practice_screen;
static lv_obj_t *title_label;
static lv_obj_t *mode_label;
static lv_obj_t *battery_label;

static void refresh_status(struct k_work *work) {
    static const char *const mode_names[] = {
        [ZEN_R4_MODE_UNKNOWN] = "Waiting for left",
        [ZEN_R4_MODE_TYPING] = "Typing",
        [ZEN_R4_MODE_SYMBOLS] = "SYM + NUM",
        [ZEN_R4_MODE_JAVELIN] = "Javelin",
        [ZEN_R4_MODE_PLOVER] = "Plover HID",
    };
    uint32_t practice = atomic_get(&practice_screen);
    if (practice & ZEN_R4_PRACTICE_ACTIVE) {
        unsigned feedback = (practice >> 2) & 3;
        unsigned round = (practice >> 4) & 15;
        unsigned streak = (practice >> 8) & 15;
        unsigned best = (practice >> 12) & 15;
        static const char *const titles[] = {"PRACTICE", "CORRECT!", "TRY AGAIN", "DRILL COMPLETE"};
        lv_label_set_text_static(title_label, titles[feedback]);
        lv_label_set_text_static(mode_label,
                                feedback == ZEN_R4_PRACTICE_DONE
                                    ? "10 / 10"
                                    : zen_r4_practice_words[practice & 3]);
        if (feedback == ZEN_R4_PRACTICE_DONE) {
            lv_label_set_text_fmt(battery_label, "Best streak %u", best);
        } else {
            lv_label_set_text_fmt(battery_label, "%u/10  Streak %u", round + 1, streak);
        }
    } else {
        lv_label_set_text_static(title_label, "ZEN R4");
        lv_label_set_text_static(mode_label, mode_names[atomic_get(&mode)]);
        lv_label_set_text_fmt(battery_label, "Right %u%%", (unsigned)zmk_battery_state_of_charge());
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
        unsigned round = (value >> 4) & 15;
        unsigned feedback = (value >> 2) & 3;
        if (!(value & ZEN_R4_PRACTICE_ACTIVE) ||
            (value & ~(ZEN_R4_PRACTICE_ACTIVE | 0xffffu)) ||
            round > ZEN_R4_PRACTICE_ROUNDS ||
            ((round == ZEN_R4_PRACTICE_ROUNDS) != (feedback == ZEN_R4_PRACTICE_DONE)) ||
            ((value >> 8) & 15) > ZEN_R4_PRACTICE_ROUNDS ||
            ((value >> 12) & 15) > ZEN_R4_PRACTICE_ROUNDS) {
            return -EINVAL;
        }
        atomic_set(&practice_screen, value);
    } else if (binding->param2 == ZEN_R4_STATUS_PROTOCOL &&
               binding->param1 <= ZEN_R4_MODE_PLOVER) {
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
ZMK_SUBSCRIPTION(zen_r4_display, zmk_split_peripheral_status_changed);
ZMK_SUBSCRIPTION(zen_r4_display, zmk_battery_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    title_label = lv_label_create(screen);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);

    mode_label = lv_label_create(screen);
    lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(mode_label, LV_ALIGN_CENTER, 0, 0);

    battery_label = lv_label_create(screen);
    lv_obj_align(battery_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    refresh_status(NULL);
    return screen;
}
