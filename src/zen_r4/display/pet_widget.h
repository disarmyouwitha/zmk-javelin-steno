#pragma once

#include <lvgl.h>

enum pet_state {
    PET_IDLE,
    PET_TYPING,
    PET_LEVEL_UP,
    PET_SLEEP,
    PET_POWERED,
};

void zen_r4_pet_create(lv_obj_t *parent);
void zen_r4_pet_set_state(enum pet_state state);
void zen_r4_pet_set_visible(bool visible);
void zen_r4_pet_set_powered(bool powered);
void zen_r4_pet_set_headroom(bool available);
