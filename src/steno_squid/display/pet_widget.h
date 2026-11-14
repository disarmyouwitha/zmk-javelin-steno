#pragma once

#include <lvgl.h>

enum pet_state {
    PET_IDLE,
    PET_TYPING,
    PET_LEVEL_UP,
    PET_SLEEP,
    PET_POWERED,
    PET_GREETING,
    PET_LAYER_CHANGE,
};

void steno_squid_pet_create(lv_obj_t *parent);
void steno_squid_pet_set_state(enum pet_state state);
void steno_squid_pet_set_visible(bool visible);
void steno_squid_pet_set_powered(bool powered);
void steno_squid_pet_set_headroom(bool available);
