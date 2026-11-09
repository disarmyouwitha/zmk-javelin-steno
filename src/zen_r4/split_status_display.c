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
    lv_label_set_text_static(mode_label, mode_names[atomic_get(&mode)]);
    lv_label_set_text_fmt(battery_label, "Right %u%%", (unsigned)zmk_battery_state_of_charge());
}

K_WORK_DEFINE(refresh_status_work, refresh_status);

static void queue_refresh(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &refresh_status_work);
    }
}

static int status_received(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    if (binding->param2 != ZEN_R4_STATUS_PROTOCOL || binding->param1 > ZEN_R4_MODE_PLOVER) {
        return -EINVAL;
    }
    atomic_set(&mode, binding->param1);
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

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text_static(title, "ZEN R4");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    mode_label = lv_label_create(screen);
    lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(mode_label, LV_ALIGN_CENTER, 0, 0);

    battery_label = lv_label_create(screen);
    lv_obj_align(battery_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    refresh_status(NULL);
    return screen;
}
