#include "screen.h"

#include <stdio.h>

#include "../split_status.h"

static lv_obj_t *welcome_page;
static lv_obj_t *run_page;
static lv_obj_t *drill_choices[ZEN_R4_PRACTICE_DRILL_COUNT];
static lv_obj_t *drill_label;
static lv_obj_t *prompt_label;
static lv_obj_t *outline_label;
static lv_obj_t *feedback_label;
static lv_obj_t *progress_bar;
static lv_obj_t *progress_label;
static lv_obj_t *best_label;

lv_obj_t *zen_r4_practice_widget_create(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(page, 0, 10);
    lv_obj_set_size(page, 68, 130);

    welcome_page = lv_obj_create(page);
    lv_obj_remove_style_all(welcome_page);
    lv_obj_remove_flag(welcome_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(welcome_page, 68, 130);
    lv_obj_t *welcome = zen_r4_display_label(welcome_page, 0, &lv_font_montserrat_12);
    lv_label_set_text_static(welcome, "Practice");
    static const char *const choices[] = {"WORDS 10", "LEFT 50", "RIGHT 50"};
    for (unsigned i = 0; i < ZEN_R4_PRACTICE_DRILL_COUNT; ++i) {
        drill_choices[i] = zen_r4_display_label(welcome_page, 23 + i * 23,
                                                &lv_font_montserrat_12);
        lv_obj_set_x(drill_choices[i], 1);
        lv_obj_set_width(drill_choices[i], 66);
        lv_obj_set_style_pad_ver(drill_choices[i], 3, 0);
        lv_obj_set_style_bg_opa(drill_choices[i], LV_OPA_COVER, 0);
        lv_label_set_text_static(drill_choices[i], choices[i]);
    }
    lv_obj_t *navigation = zen_r4_display_label(welcome_page, 98, &lv_font_unscii_8);
    lv_label_set_text_static(navigation, "*L / *R");
    lv_obj_t *start = zen_r4_display_label(welcome_page, 115, &lv_font_unscii_8);
    lv_label_set_text_static(start, "A/O: GO");

    run_page = lv_obj_create(page);
    lv_obj_remove_style_all(run_page);
    lv_obj_remove_flag(run_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(run_page, 68, 130);

    drill_label = zen_r4_display_label(run_page, 0, &lv_font_montserrat_12);
    prompt_label = zen_r4_display_label(run_page, 15, &lv_font_montserrat_28);
    outline_label = zen_r4_display_label(run_page, 48, &lv_font_montserrat_12);
    feedback_label = zen_r4_display_label(run_page, 67, &lv_font_unscii_8);
    lv_obj_set_x(feedback_label, 1);
    lv_obj_set_width(feedback_label, 66);
    lv_obj_set_style_pad_ver(feedback_label, 2, 0);
    lv_obj_set_style_text_color(feedback_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(feedback_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(feedback_label, LV_OPA_COVER, 0);

    progress_bar = zen_r4_display_bar(run_page, 4, 86, 60, 8);
    progress_label = zen_r4_display_label(run_page, 97, &lv_font_montserrat_8);
    best_label = zen_r4_display_label(run_page, 113, &lv_font_montserrat_12);
    return page;
}

void zen_r4_practice_widget_update(uint32_t practice) {
    static const char *const drill_names[] = {"WORDS", "LEFT", "RIGHT"};
    static const char *const feedback_names[] = {"READY", "CORRECT", "RETRY", "DONE"};
    static uint32_t last_practice;
    if (last_practice == practice) {
        return;
    }
    const unsigned feedback = zen_r4_practice_feedback_value(practice);
    last_practice = practice;
    const unsigned round = zen_r4_practice_counter(practice, ZEN_R4_PRACTICE_ROUND_SHIFT);
    const unsigned streak = zen_r4_practice_counter(practice, ZEN_R4_PRACTICE_STREAK_SHIFT);
    const unsigned best = zen_r4_practice_counter(practice, ZEN_R4_PRACTICE_BEST_SHIFT);
    const enum zen_r4_practice_drill drill =
        (practice & ZEN_R4_PRACTICE_DRILL_MASK) >> ZEN_R4_PRACTICE_DRILL_SHIFT;
    const bool menu = (practice & ZEN_R4_PRACTICE_MENU) != 0;
    if (menu) {
        lv_obj_remove_flag(welcome_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(run_page, LV_OBJ_FLAG_HIDDEN);
        for (unsigned i = 0; i < ZEN_R4_PRACTICE_DRILL_COUNT; ++i) {
            const bool selected = i == drill;
            lv_obj_set_style_bg_color(drill_choices[i],
                                      selected ? lv_color_black() : lv_color_white(), 0);
            lv_obj_set_style_text_color(drill_choices[i],
                                        selected ? lv_color_white() : lv_color_black(), 0);
        }
        return;
    }
    lv_obj_add_flag(welcome_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(run_page, LV_OBJ_FLAG_HIDDEN);
    const unsigned total = (practice & ZEN_R4_PRACTICE_LEGACY) && drill != ZEN_R4_PRACTICE_WORDS
                               ? 30 : zen_r4_practice_rounds(drill);
    const bool words = drill == ZEN_R4_PRACTICE_WORDS;
    const bool done = feedback == ZEN_R4_PRACTICE_DONE;
    const bool no_dictionary = (practice & ZEN_R4_PRACTICE_NO_DICTIONARY) != 0;
    const unsigned completed = round + (feedback == ZEN_R4_PRACTICE_CORRECT);
    const char *outline = zen_r4_practice_outline(drill, practice & ZEN_R4_PRACTICE_PROMPT_MASK);
    const char *prompt = words ? zen_r4_practice_words[practice & ZEN_R4_PRACTICE_PROMPT_MASK] : outline;
    char progress[32];

    lv_label_set_text_static(drill_label, done ? "A: MENU" : drill_names[drill]);
    lv_obj_set_style_text_font(prompt_label, done ? &lv_font_montserrat_16 : &lv_font_montserrat_28, 0);
    lv_label_set_text_static(prompt_label, done ? "DONE" : prompt);
    lv_label_set_text_static(outline_label, no_dictionary ? "Enable dict" :
                                           words ? outline : "Tap key");
    lv_obj_set_y(progress_label, done ? 91 : 97);
    lv_obj_set_style_text_font(progress_label,
                               done ? &lv_font_montserrat_16 : &lv_font_montserrat_8, 0);
    if (done) {
        lv_obj_add_flag(outline_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(best_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(best_label, "Best %u", best);
        snprintf(progress, sizeof(progress), "%u/%u", total, total);
    } else {
        lv_obj_remove_flag(outline_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(best_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_static(feedback_label,
                                 no_dictionary ? "DICT OFF" : feedback_names[feedback]);
        lv_bar_set_range(progress_bar, 0, total);
        lv_bar_set_value(progress_bar, completed, LV_ANIM_OFF);
        snprintf(progress, sizeof(progress), "%u/%u  Streak %u", round + 1, total, streak);
    }
    lv_label_set_text(progress_label, progress);
}
