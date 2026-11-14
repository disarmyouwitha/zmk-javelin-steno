#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "../stroke_feed.h"

lv_obj_t *zen_r4_screen_create(void);
/* The last stroke, its translation and the typing speed. Shown on the Javelin
 * layer, below the pet, until the next mode or layer change clears it.
 */
void zen_r4_screen_set_tape(const struct zen_r4_tape *tape);
void zen_r4_screen_update(unsigned mode, const char *layer_name, uint32_t practice, unsigned charge);

lv_obj_t *zen_r4_display_label(lv_obj_t *parent, int y, const lv_font_t *font);
lv_obj_t *zen_r4_display_bar(lv_obj_t *parent, int x, int y, int width, int height);

lv_obj_t *zen_r4_practice_widget_create(lv_obj_t *parent);
void zen_r4_practice_widget_update(uint32_t practice);
