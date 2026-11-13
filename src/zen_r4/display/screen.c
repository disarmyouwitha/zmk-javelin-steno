#include "screen.h"

#include <stdio.h>
#include <string.h>

#include "../split_status.h"
#include "pet_widget.h"
#include "portrait.h"

static lv_obj_t *status_page;
static lv_obj_t *practice_page;
static lv_obj_t *mode_label;
static lv_obj_t *mode_continuation;
static lv_obj_t *battery_label;
static lv_obj_t *battery_bar;
static bool was_done;

lv_obj_t *zen_r4_display_label(lv_obj_t *parent, int y, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

#define MODE_LABEL_WIDTH 66
#define MODE_TEXT_WIDTH (MODE_LABEL_WIDTH - 4)

static lv_obj_t *mode_badge(lv_obj_t *parent, int y) {
    /* The 1-bit font keeps the white glyphs crisp on a black badge. */
    lv_obj_t *label = zen_r4_display_label(parent, y, &lv_font_unscii_8);
    lv_obj_set_x(label, 1);
    lv_obj_set_width(label, MODE_LABEL_WIDTH);
    lv_obj_set_style_pad_hor(label, 2, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);
    lv_obj_set_style_bg_color(label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_radius(label, 2, 0);
    return label;
}

static void set_badge(lv_obj_t *label, const char *text) {
    if (strcmp(text, lv_label_get_text(label)) != 0) {
        lv_label_set_text(label, text);
    }
}

static bool fits_on_both_badges(const char *title, size_t split, size_t length) {
    return title[split] == ' ' &&
           lv_text_get_width(title, split, &lv_font_unscii_8, 0) <= MODE_TEXT_WIDTH &&
           lv_text_get_width(title + split + 1, length - split - 1, &lv_font_unscii_8, 0) <=
               MODE_TEXT_WIDTH;
}

static void show_mode(const char *title) {
    const lv_font_t *font = &lv_font_unscii_8;
    size_t length = strlen(title);
    const char *separator = strchr(title, '\n');
    if (!separator && lv_text_get_width(title, length, font, 0) <= MODE_TEXT_WIDTH) {
        set_badge(mode_label, title);
        lv_obj_add_flag(mode_continuation, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Prefer the word break closest to the middle. Unbroken custom names
     * split at the midpoint, so all nine characters stay visible.
     */
    if (!separator) {
        for (size_t distance = 0; distance < length / 2; distance++) {
            size_t middle = length / 2;
            if (fits_on_both_badges(title, middle - distance, length)) {
                separator = title + middle - distance;
                break;
            }
            if (fits_on_both_badges(title, middle + distance, length)) {
                separator = title + middle + distance;
                break;
            }
        }
    }
    size_t split = separator ? (size_t)(separator - title) : length / 2;
    char first[ZEN_R4_LAYER_NAME_CHARS + 1];
    snprintf(first, sizeof(first), "%.*s", (int)split, title);
    set_badge(mode_label, first);
    set_badge(mode_continuation, title + split + (separator != NULL));
    lv_obj_remove_flag(mode_continuation, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *zen_r4_display_bar(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, width, height);
    lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    return bar;
}

lv_obj_t *zen_r4_screen_create(void) {
    zen_r4_portrait_init();
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    status_page = lv_obj_create(screen);
    lv_obj_remove_style_all(status_page);
    lv_obj_remove_flag(status_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(status_page, 68, 135);
    mode_label = mode_badge(status_page, 17);
    mode_continuation = mode_badge(status_page, 108);
    practice_page = zen_r4_practice_widget_create(screen);

    /* Rounded case and side terminal inspired by nice!view Elemental. */
    battery_bar = zen_r4_display_bar(screen, 5, 143, 23, 11);
    lv_obj_set_style_radius(battery_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_bar, 2, LV_PART_INDICATOR);
    lv_obj_t *terminal = lv_obj_create(screen);
    lv_obj_remove_style_all(terminal);
    lv_obj_set_pos(terminal, 28, 146);
    lv_obj_set_size(terminal, 2, 5);
    lv_obj_set_style_bg_color(terminal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
    battery_label = zen_r4_display_label(screen, 142, &lv_font_montserrat_10);
    lv_obj_set_x(battery_label, 33);
    lv_obj_set_width(battery_label, 30);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    zen_r4_pet_create(screen);
    return screen;
}

void zen_r4_screen_update(unsigned mode, const char *layer_name, uint32_t practice, unsigned charge) {
    static const char *const mode_names[] = {
        [ZEN_R4_MODE_UNKNOWN] = "Waiting\nFor Left",
        [ZEN_R4_MODE_TYPING] = "Typing",
        [ZEN_R4_MODE_SYMBOLS] = "SYM+NUM",
        [ZEN_R4_MODE_JAVELIN] = "Javelin",
        [ZEN_R4_MODE_PLOVER] = "PloverHID",
        [ZEN_R4_MODE_LOADING] = "Loading...",
        [ZEN_R4_MODE_NO_DICTIONARY] = "No Dict",
    };
    char battery[8];
    snprintf(battery, sizeof(battery), "%u%%", charge);
    if (strcmp(battery, lv_label_get_text(battery_label)) != 0) {
        lv_label_set_text(battery_label, battery);
        lv_bar_set_value(battery_bar, charge, LV_ANIM_OFF);
    }

    const bool practicing = (practice & ZEN_R4_PRACTICE_ACTIVE) != 0;
    const bool done = practicing && ((practice >> 2) & 3) == ZEN_R4_PRACTICE_DONE;
    zen_r4_pet_set_visible(!practicing || done);
    if (practicing) {
        lv_obj_add_flag(status_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(practice_page, LV_OBJ_FLAG_HIDDEN);
        zen_r4_practice_widget_update(practice);
    } else {
        lv_obj_add_flag(practice_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(status_page, LV_OBJ_FLAG_HIDDEN);
        const char *title = mode < ZEN_R4_MODE_COUNT ? mode_names[mode] : layer_name;
        show_mode(title);
    }
    if (done != was_done) {
        zen_r4_pet_set_state(done ? PET_LEVEL_UP : PET_IDLE);
    }
    was_done = done;
}
