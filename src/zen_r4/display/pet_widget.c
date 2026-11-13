#include "pet_widget.h"

#include <zephyr/sys/util.h>

#define PET_SIZE 48
#define PET_IMAGE(name)                                                                            \
    static const lv_image_dsc_t name##_image = {                                                     \
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = PET_SIZE,            \
                   .h = PET_SIZE, .stride = PET_SIZE},                                                \
        .data_size = sizeof(name##_pixels),                                                         \
        .data = name##_pixels,                                                                     \
    }
#include "pet_sprites.h"
#undef PET_IMAGE

BUILD_ASSERT(sizeof(idle_pixels) == PET_SIZE * PET_SIZE);

#define FX_IMAGE(name)                                                                             \
    static const lv_image_dsc_t name##_image = {                                                    \
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,                         \
                   .w = 48, .h = 30, .stride = 48},                                                \
        .data_size = sizeof(name##_pixels), .data = name##_pixels,                                  \
    }
#include "pet_effects.h"
#undef FX_IMAGE

struct pet_frame {
    const lv_image_dsc_t *image;
    uint16_t duration_ms;
};

struct pet_animation {
    const struct pet_frame *frames;
    uint8_t count;
};

static const struct pet_frame typing_frames[] = {
    {&typing_image, 130}, {&idle_image, 650}, {&blink_image, 110},
};
static const struct pet_frame level_up_frames[] = {
    {&jump_image, 120}, {&spark_image, 180}, {&land_image, 100}, {&blink_image, 120},
};
static const struct pet_frame powered_frames[] = {
    {&idle_image, 220}, {&idle_image, 180}, {&jump_image, 180}, {&idle_image, 180},
    {&idle_image, 180}, {&blink_image, 130}, {&idle_image, 180}, {&idle_image, 1400},
};
static const lv_image_dsc_t *const powered_effects[] = {
    &rising_0_image, &rising_1_image, &rising_2_image, &rising_3_image,
    &rising_4_image, &rising_5_image, &rising_6_image, &rising_7_image,
};
static const int8_t powered_bob[] = {0, -1, -2, -3, -2, -1, 0, 0};
BUILD_ASSERT(ARRAY_SIZE(powered_effects) == ARRAY_SIZE(powered_frames));
BUILD_ASSERT(ARRAY_SIZE(powered_bob) == ARRAY_SIZE(powered_frames));

#define ANIMATION(frames) {frames, sizeof(frames) / sizeof((frames)[0])}
static const struct pet_animation animations[] = {
    [PET_TYPING] = ANIMATION(typing_frames),
    [PET_LEVEL_UP] = ANIMATION(level_up_frames),
    [PET_POWERED] = ANIMATION(powered_frames),
};

static lv_obj_t *pet_image;
static lv_obj_t *effect_image;
static lv_timer_t *frame_timer;
static enum pet_state state;
static uint8_t frame_index;
static bool usb_powered;
static bool pet_visible = true;
static bool headroom = true;

static void update_effect(void) {
    if (pet_visible && headroom && state == PET_POWERED) {
        lv_image_set_src(effect_image, powered_effects[frame_index]);
        lv_obj_remove_flag(effect_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(effect_image, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_frame(void) {
    const struct pet_animation *animation = &animations[state];
    const struct pet_frame *frame = &animation->frames[frame_index];
    lv_image_set_src(pet_image, frame->image);
    lv_obj_set_y(pet_image, 51 + (state == PET_POWERED ? powered_bob[frame_index] : 0));
    update_effect();
    lv_timer_set_period(frame_timer, frame->duration_ms);
    lv_timer_reset(frame_timer);
    lv_timer_resume(frame_timer);
}

static void advance_frame(lv_timer_t *timer) {
    frame_index++;
    if (frame_index == animations[state].count) {
        if (state == PET_POWERED) {
            frame_index = 0;
            show_frame();
        } else {
            zen_r4_pet_set_state(PET_IDLE);
        }
    } else {
        show_frame();
    }
}

void zen_r4_pet_create(lv_obj_t *parent) {
    effect_image = lv_image_create(parent);
    lv_obj_remove_style_all(effect_image);
    lv_obj_set_pos(effect_image, 10, 12);
    lv_obj_set_style_image_recolor(effect_image, lv_color_black(), 0);
    lv_image_set_src(effect_image, &rising_7_image);
    lv_obj_add_flag(effect_image, LV_OBJ_FLAG_HIDDEN);
    pet_image = lv_image_create(parent);
    lv_obj_remove_style_all(pet_image);
    lv_obj_set_pos(pet_image, 10, 51);
    lv_obj_set_style_image_recolor(pet_image, lv_color_black(), 0);
    lv_image_set_src(pet_image, &idle_image);
    frame_timer = lv_timer_create(advance_frame, 100, NULL);
    lv_timer_pause(frame_timer);
}

void zen_r4_pet_set_state(enum pet_state next) {
    if (!pet_image) {
        return;
    }

    if (!pet_visible) {
        next = PET_IDLE;
    } else if (usb_powered && (next == PET_IDLE || next == PET_SLEEP)) {
        next = PET_POWERED;
    }
    if (next == PET_TYPING && state != PET_IDLE && state != PET_TYPING &&
        state != PET_POWERED) {
        return;
    }
    if (next == PET_TYPING && state == PET_TYPING && frame_index == 0) {
        lv_timer_reset(frame_timer);
        return;
    }
    if (next == state && (state == PET_IDLE || state == PET_SLEEP || state == PET_POWERED)) {
        return;
    }
    state = next;
    frame_index = 0;
    if (state == PET_IDLE || state == PET_SLEEP) {
        lv_timer_pause(frame_timer);
        lv_image_set_src(pet_image, state == PET_SLEEP ? &sleep_image : &idle_image);
        lv_obj_set_y(pet_image, 51);
        update_effect();
    } else {
        show_frame();
    }
}

void zen_r4_pet_set_visible(bool visible) {
    if (!pet_image || pet_visible == visible) {
        return;
    }
    pet_visible = visible;
    if (visible) {
        lv_obj_remove_flag(pet_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(pet_image, LV_OBJ_FLAG_HIDDEN);
    }
    zen_r4_pet_set_state(PET_IDLE);
}

void zen_r4_pet_set_powered(bool powered) {
    if (usb_powered == powered) {
        return;
    }
    usb_powered = powered;
    if (state == PET_IDLE || state == PET_SLEEP || state == PET_POWERED) {
        zen_r4_pet_set_state(PET_IDLE);
    }
}

void zen_r4_pet_set_headroom(bool available) {
    headroom = available;
    if (pet_image) {
        update_effect();
    }
}
