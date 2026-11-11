#define DT_DRV_COMPAT zmk_behavior_zen_r4_status

#include <drivers/behavior.h>
#include <lvgl.h>
#include <stdio.h>
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

/* Past the modes the left can send: show the layer name it sent instead. */
#define MODE_LAYER_NAME ZEN_R4_MODE_COUNT

static atomic_t mode;
static atomic_t layer_name_low;
static atomic_t layer_name_high;
static atomic_t practice_screen;
static lv_obj_t *status_canvas;
#define PORTRAIT_WIDTH 68
#define PORTRAIT_HEIGHT 160
#define CANVAS_FORMAT LV_COLOR_FORMAT_L8
static uint8_t portrait_buffer[LV_CANVAS_BUF_SIZE(68, 160, 8, LV_DRAW_BUF_STRIDE_ALIGN)]
    __aligned(LV_DRAW_BUF_ALIGN);
static uint8_t landscape_buffer[LV_CANVAS_BUF_SIZE(160, 68, 8, LV_DRAW_BUF_STRIDE_ALIGN)]
    __aligned(LV_DRAW_BUF_ALIGN);

static void draw_text(lv_layer_t *layer, int y, int height, const lv_font_t *font,
                      const char *text, bool bold) {
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = font;
    label.color = lv_color_black();
    label.align = LV_TEXT_ALIGN_CENTER;
    label.text = text;
    label.text_local = 1;
    lv_area_t area = {4, y, layer->buf_area.x2 - 4, y + height - 1};
    lv_draw_label(layer, &label, &area);
    if (bold) {
        // A one-pixel horizontal overstrike keeps the prompt readable at 1 bit.
        area.x1++;
        area.x2++;
        lv_draw_label(layer, &label, &area);
    }
}

static void draw_rect(lv_layer_t *layer, int x, int y, int width, int height, bool black) {
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = black ? lv_color_black() : lv_color_white();
    rect.bg_opa = LV_OPA_COVER;
    lv_area_t area = {x, y, x + width - 1, y + height - 1};
    lv_draw_rect(layer, &rect, &area);
}

static void draw_practice(lv_layer_t *layer, uint32_t practice) {
    unsigned feedback = (practice >> 2) & 3;
    unsigned round = (practice >> 4) & 15;
    unsigned streak = (practice >> 8) & 15;
    unsigned best = (practice >> 12) & 15;
    enum zen_r4_practice_drill drill =
        (practice & ZEN_R4_PRACTICE_DRILL_MASK) >> ZEN_R4_PRACTICE_DRILL_SHIFT;
    bool words = drill == ZEN_R4_PRACTICE_WORDS;
    bool no_dictionary = practice & ZEN_R4_PRACTICE_NO_DICTIONARY;
    bool done = feedback == ZEN_R4_PRACTICE_DONE;
    unsigned completed = round + (feedback == ZEN_R4_PRACTICE_CORRECT);
    char text[24];

    static const char *const drill_names[] = {"WORDS", "LEFT", "RIGHT"};
    const char *outline = zen_r4_practice_outline(drill, round);
    const char *prompt = words ? zen_r4_practice_words[practice & 3] : outline;
    draw_text(layer, 3, 16, &lv_font_montserrat_12, drill_names[drill], true);
    draw_rect(layer, 8, 21, 52, 1, true);
    if (no_dictionary) {
        draw_text(layer, 27, 24, &lv_font_montserrat_10, "ENABLE DICT", false);
    } else {
        draw_text(layer, 26, 19, &lv_font_montserrat_16, "<       >", false);
    }
    draw_text(layer, 47, 34, done ? &lv_font_montserrat_16 : &lv_font_montserrat_28,
              done ? "DONE" : prompt, true);
    draw_rect(layer, 10, 81, 48, 1, true);
    draw_text(layer, 86, 20, words || done ? &lv_font_montserrat_16 : &lv_font_montserrat_12,
              done ? "10 / 10" : words ? outline : "TAP KEY", false);
    static const char *const feedback_text[] = {"READY", "CORRECT", "RETRY", "DONE"};
    draw_rect(layer, 3, 107, 62, 22, true);
    draw_rect(layer, 4, 108, 60, 20, false);
    draw_text(layer, 111, 16,
              feedback == ZEN_R4_PRACTICE_CORRECT ? &lv_font_montserrat_10 : &lv_font_montserrat_12,
              no_dictionary ? "DICT OFF" : feedback_text[feedback], false);
    snprintf(text, sizeof(text), "%s %u", done ? "BEST" : "STREAK", done ? best : streak);
    draw_text(layer, 132, 12, &lv_font_montserrat_10, text, false);
    draw_rect(layer, 8, 145, 52, 4, true);
    draw_rect(layer, 9, 146, 50, 2, false);
    if (completed) {
        draw_rect(layer, 9, 146, completed * 5, 2, true);
    }
    snprintf(text, sizeof(text), "%02u / 10", done ? 10 : round + 1);
    draw_text(layer, 150, 10, &lv_font_montserrat_10, text, false);
}

static void refresh_status(struct k_work *work) {
    uint32_t practice = atomic_get(&practice_screen);
    bool portrait = (practice & ZEN_R4_PRACTICE_ACTIVE) != 0;
    lv_canvas_set_buffer(status_canvas, portrait ? portrait_buffer : landscape_buffer,
                         portrait ? PORTRAIT_WIDTH : PORTRAIT_HEIGHT,
                         portrait ? PORTRAIT_HEIGHT : PORTRAIT_WIDTH, CANVAS_FORMAT);
    lv_canvas_fill_bg(status_canvas, lv_color_white(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(status_canvas, &layer);
    if (portrait) {
        draw_practice(&layer, practice);
    } else {
        static const char *const mode_names[] = {
            [ZEN_R4_MODE_UNKNOWN] = "Waiting for left",
            [ZEN_R4_MODE_TYPING] = "Typing",
            [ZEN_R4_MODE_SYMBOLS] = "SYM + NUM",
            [ZEN_R4_MODE_JAVELIN] = "Javelin",
            [ZEN_R4_MODE_PLOVER] = "Plover HID",
            [ZEN_R4_MODE_LOADING] = "Loading...",
            [ZEN_R4_MODE_NO_DICTIONARY] = "No dictionary",
        };
        draw_text(&layer, 4, 15, &lv_font_montserrat_12, "ZEN R4", false);
        char text[24];
        const atomic_val_t shown = atomic_get(&mode);
        char name[ZEN_R4_LAYER_NAME_CHARS + 1];
        const char *title = name;
        if (shown == MODE_LAYER_NAME) {
            const uint64_t packed = (uint64_t)(uint32_t)atomic_get(&layer_name_high) << 32 |
                                    (uint32_t)atomic_get(&layer_name_low);
            for (int i = 0; i < ZEN_R4_LAYER_NAME_CHARS; i++) {
                name[i] = (packed >> (7 * i)) & 0x7f;
            }
            name[ZEN_R4_LAYER_NAME_CHARS] = 0;
        } else {
            title = mode_names[shown];
        }
        draw_text(&layer, 24, 20, &lv_font_montserrat_16, title, false);
        snprintf(text, sizeof(text), "Right %u%%", (unsigned)zmk_battery_state_of_charge());
        draw_text(&layer, 51, 14, &lv_font_montserrat_10, text, false);
    }
    lv_canvas_finish_layer(status_canvas, &layer);
    if (!portrait) {
        return;
    }
    // Invert the complete L8 screen, including text, outlines and progress.
    for (size_t i = 0; i < sizeof(portrait_buffer); i++) {
        portrait_buffer[i] = UINT8_MAX - portrait_buffer[i];
    }
    // Match the stock nice!view widget's portrait orientation.
    lv_draw_sw_rotate(portrait_buffer, landscape_buffer, PORTRAIT_WIDTH, PORTRAIT_HEIGHT,
                      lv_draw_buf_width_to_stride(PORTRAIT_WIDTH, CANVAS_FORMAT),
                      lv_draw_buf_width_to_stride(PORTRAIT_HEIGHT, CANVAS_FORMAT),
                      LV_DISPLAY_ROTATION_270, CANVAS_FORMAT);
    lv_canvas_set_buffer(status_canvas, landscape_buffer, PORTRAIT_HEIGHT, PORTRAIT_WIDTH,
                         CANVAS_FORMAT);
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
            (value & ~(ZEN_R4_PRACTICE_ACTIVE | ZEN_R4_PRACTICE_NO_DICTIONARY |
                       ZEN_R4_PRACTICE_DRILL_MASK | 0xffffu)) ||
            (value & ZEN_R4_PRACTICE_DRILL_MASK) >=
                (ZEN_R4_PRACTICE_DRILL_COUNT << ZEN_R4_PRACTICE_DRILL_SHIFT) ||
            round > ZEN_R4_PRACTICE_ROUNDS ||
            ((round == ZEN_R4_PRACTICE_ROUNDS) != (feedback == ZEN_R4_PRACTICE_DONE)) ||
            ((value >> 8) & 15) > ZEN_R4_PRACTICE_ROUNDS ||
            ((value >> 12) & 15) > ZEN_R4_PRACTICE_ROUNDS) {
            return -EINVAL;
        }
        atomic_set(&practice_screen, value);
    } else if ((binding->param2 & 0xff) == ZEN_R4_LAYER_NAME_PROTOCOL) {
        atomic_clear(&practice_screen);
        atomic_set(&layer_name_low, binding->param1);
        atomic_set(&layer_name_high, binding->param2 >> 8);
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

    status_canvas = lv_canvas_create(screen);
    lv_obj_set_pos(status_canvas, 0, 0);
    refresh_status(NULL);
    return screen;
}
