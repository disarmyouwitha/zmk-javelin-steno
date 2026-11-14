#pragma once

#include <lvgl.h>
#include <stdint.h>

lv_obj_t *zen_r4_screen_create(void);
void zen_r4_screen_update(unsigned mode, const char *layer_name, uint32_t practice, unsigned charge,
                         bool host_disconnected);

lv_obj_t *zen_r4_display_label(lv_obj_t *parent, int y, const lv_font_t *font);
lv_obj_t *zen_r4_display_bar(lv_obj_t *parent, int x, int y, int width, int height);

lv_obj_t *zen_r4_practice_widget_create(lv_obj_t *parent);
void zen_r4_practice_widget_update(uint32_t practice);
