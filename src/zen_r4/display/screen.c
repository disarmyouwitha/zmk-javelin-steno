#include "screen.h"

#include <stdio.h>
#include <string.h>

#include "../split_status.h"
#include "pet_widget.h"
#include "portrait.h"

static lv_obj_t *status_page;
static lv_obj_t *practice_page;
static lv_obj_t *mode_below;
static lv_obj_t *mode_above;
static lv_obj_t *battery_label;
static lv_obj_t *battery_bar;
static bool was_done;
static bool had_layer;
static unsigned previous_layer_mode;
static char previous_layer_title[ZEN_R4_LAYER_NAME_CHARS + 1];

static void draw_bold_text(lv_event_t *event) {
    lv_obj_t *label = lv_event_get_target_obj(event);
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    lv_obj_init_draw_label_dsc(label, LV_PART_MAIN, &descriptor);
    descriptor.text = lv_label_get_text(label);
    descriptor.text_local = true;
    lv_area_t area;
    lv_obj_get_content_coords(label, &area);
    area.x1++;
    area.x2++;
    lv_draw_label(lv_event_get_layer(event), &descriptor, &area);
}

lv_obj_t *zen_r4_display_label(lv_obj_t *parent, int y, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(label, draw_bold_text, LV_EVENT_DRAW_MAIN_END, NULL);
    return label;
}

#define MODE_TEXT_WIDTH 67

static const lv_font_t *const mode_fonts[] = {
    &lv_font_montserrat_20, &lv_font_montserrat_16, &lv_font_montserrat_14,
};

static const lv_font_t *fit_mode_font(const char *text, size_t length) {
    for (size_t i = 0; i < sizeof(mode_fonts) / sizeof(mode_fonts[0]); i++) {
        if (lv_text_get_width(text, length, mode_fonts[i], 0) <= MODE_TEXT_WIDTH) {
            return mode_fonts[i];
        }
    }
    return NULL;
}

static void set_mode_label(lv_obj_t *label, const char *text) {
    if (strcmp(text, lv_label_get_text(label)) != 0) {
        const lv_font_t *font = strchr(text, '\n') ? &lv_font_montserrat_16 :
                                fit_mode_font(text, strlen(text));
        lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
        lv_label_set_text(label, text);
    }
}

static void show_mode(const char *title) {
    /* Keep Waiting above the squid; place For Left on the lower text row.
     * Every other mode starts on the usual first row below the squid.
     */
    lv_obj_set_y(mode_below, strcmp(title, "Waiting\nFor Left") == 0 ? 119 : 99);
    if (strcmp(title, "PloverHID") == 0) {
        set_mode_label(mode_below, "Plover\nHID");
        lv_obj_add_flag(mode_above, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char *separator = strchr(title, '\n');
    if (!separator) {
        const size_t length = strlen(title);
        if (fit_mode_font(title, length)) {
            set_mode_label(mode_below, title);
        } else {
            /* The left half sends at most nine ASCII characters. Fit a
             * wide name on two lines below the pet, preferring a space.
             */
            size_t split = length / 2;
            const char *space = strchr(title, ' ');
            if (space && space - title >= 3 && space - title <= 6 && space[1]) {
                split = space - title;
            }
            char lines[ZEN_R4_LAYER_NAME_CHARS + 2];
            snprintf(lines, sizeof(lines), "%.*s\n%s", (int)split, title,
                     title + split + (title[split] == ' '));
            if (strcmp(lines, lv_label_get_text(mode_below)) != 0) {
                /* Each half must fit the width. Drop to the 12 px font if not. */
                const size_t rest = length - split - (title[split] == ' ');
                const lv_font_t *font =
                    lv_text_get_width(title, split, &lv_font_montserrat_16, 0) <= MODE_TEXT_WIDTH &&
                            lv_text_get_width(title + split + (title[split] == ' '), rest,
                                              &lv_font_montserrat_16, 0) <= MODE_TEXT_WIDTH
                        ? &lv_font_montserrat_16
                        : &lv_font_montserrat_12;
                lv_obj_set_style_text_font(mode_below, font, 0);
                lv_label_set_text(mode_below, lines);
            }
        }
        lv_obj_add_flag(mode_above, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char first[ZEN_R4_LAYER_NAME_CHARS + 1];
    snprintf(first, sizeof(first), "%.*s", (int)(separator - title), title);
    set_mode_label(mode_above, first);
    set_mode_label(mode_below, separator + 1);
    lv_obj_remove_flag(mode_above, LV_OBJ_FLAG_HIDDEN);
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

#ifdef CONFIG_ZMK_ZEN_R4_FONT_SAMPLER
    static const struct {
        int y;
        const lv_font_t *font;
        const char *text;
        bool bold;
    } samples[] = {
        {5, &lv_font_montserrat_20, "Plover", false},
        {44, &lv_font_montserrat_16, "Qwerty", false},
        {77, &lv_font_montserrat_16, "Qwerty", true},
        {110, &lv_font_montserrat_16, "Plover\nHID", true},
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        lv_obj_t *label = zen_r4_display_label(screen, samples[i].y, samples[i].font);
        lv_label_set_text_static(label, samples[i].text);
        if (samples[i].bold) {
            /* A one-pixel overstrike thickens strokes without changing advance. */
            lv_obj_t *overstrike = zen_r4_display_label(screen, samples[i].y, samples[i].font);
            lv_obj_set_x(overstrike, 1);
            lv_label_set_text_static(overstrike, samples[i].text);
        }
    }
    return screen;
#endif

    status_page = lv_obj_create(screen);
    lv_obj_remove_style_all(status_page);
    lv_obj_remove_flag(status_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(status_page, 68, 140);
    mode_above = zen_r4_display_label(status_page, 17, &lv_font_montserrat_16);
    mode_below = zen_r4_display_label(status_page, 99, &lv_font_montserrat_16);
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
#ifdef CONFIG_ZMK_ZEN_R4_FONT_SAMPLER
    return;
#endif
    static const char *const mode_names[] = {
        [ZEN_R4_MODE_UNKNOWN] = "Waiting\nFor Left",
        [ZEN_R4_MODE_TYPING] = "Qwerty",
        [ZEN_R4_MODE_SYMBOLS] = "Numpad",
        [ZEN_R4_MODE_JAVELIN] = "Javelin",
        [ZEN_R4_MODE_PLOVER] = "PloverHID",
        [ZEN_R4_MODE_LOADING] = "Loading",
        [ZEN_R4_MODE_NO_DICTIONARY] = "No Dict",
    };
    char battery[8];
    snprintf(battery, sizeof(battery), "%u%%", charge);
    if (strcmp(battery, lv_label_get_text(battery_label)) != 0) {
        lv_label_set_text(battery_label, battery);
        lv_bar_set_value(battery_bar, charge, LV_ANIM_OFF);
    }

    bool layer_changed = false;
    const bool practicing = (practice & ZEN_R4_PRACTICE_ACTIVE) != 0;
    const bool done = practicing && zen_r4_practice_feedback_value(practice) == ZEN_R4_PRACTICE_DONE;
    zen_r4_pet_set_visible(!practicing || done);
    if (practicing) {
        had_layer = false;
        lv_obj_add_flag(status_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(practice_page, LV_OBJ_FLAG_HIDDEN);
        zen_r4_practice_widget_update(practice);
    } else {
        lv_obj_add_flag(practice_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(status_page, LV_OBJ_FLAG_HIDDEN);
        const char *title = mode < ZEN_R4_MODE_COUNT ? mode_names[mode] : layer_name;
        show_mode(title);
        const bool valid_layer = (mode >= ZEN_R4_MODE_TYPING && mode <= ZEN_R4_MODE_PLOVER) ||
                                 mode >= ZEN_R4_MODE_COUNT;
        if (valid_layer) {
            layer_changed = had_layer && (mode != previous_layer_mode ||
                                         strcmp(title, previous_layer_title) != 0);
            previous_layer_mode = mode;
            snprintf(previous_layer_title, sizeof(previous_layer_title), "%s", title);
            had_layer = true;
        } else if (mode == ZEN_R4_MODE_UNKNOWN) {
            had_layer = false;
        }
    }
    if (done != was_done) {
        zen_r4_pet_set_state(done ? PET_LEVEL_UP : PET_IDLE);
    }
    zen_r4_pet_set_headroom(done || (!practicing &&
                            lv_obj_has_flag(mode_above, LV_OBJ_FLAG_HIDDEN)));
    if (layer_changed) {
        zen_r4_pet_set_state(PET_LAYER_CHANGE);
    }
    was_done = done;
}
