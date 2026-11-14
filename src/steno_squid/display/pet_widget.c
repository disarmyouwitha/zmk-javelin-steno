#include "pet_widget.h"

#include <zephyr/sys/util.h>
#include <zmk/activity.h>

#define PET_SIZE 48
#define PET_IMAGE(name)                                                                            \
    BUILD_ASSERT(sizeof(name##_pixels) == PET_SIZE * PET_SIZE, #name " sprite size");              \
    static const lv_image_dsc_t name##_image = {                                                     \
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = PET_SIZE,            \
                   .h = PET_SIZE, .stride = PET_SIZE},                                                \
        .data_size = sizeof(name##_pixels),                                                         \
        .data = name##_pixels,                                                                     \
    }
#include "pet_sprites.h"
#undef PET_IMAGE

BUILD_ASSERT(sizeof(idle_pixels) == PET_SIZE * PET_SIZE);

#define FX_SIZED_IMAGE(name, width, height)                                                         \
    BUILD_ASSERT(sizeof(name##_pixels) == (width) * (height), #name " effect size");               \
    static const lv_image_dsc_t name##_image = {                                                    \
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,                         \
                   .w = width, .h = height, .stride = width},                                      \
        .data_size = sizeof(name##_pixels), .data = name##_pixels,                                  \
    }
#define FX_IMAGE(name) FX_SIZED_IMAGE(name, 48, 30)
#include "pet_effects.h"
#undef FX_IMAGE
#undef FX_SIZED_IMAGE

struct pet_frame {
    const lv_image_dsc_t *image;
    uint16_t duration_ms;
};

struct pet_animation {
    const struct pet_frame *frames;
    uint8_t count;
    const lv_image_dsc_t *const *effects;
    int8_t effect_x;
    int8_t effect_y;
};

static const struct pet_frame typing_frames[] = {
    {&typing_image, 110}, {&idle_image, 110}, {&blink_image, 110}, {&idle_image, 90},
};
static const struct pet_frame level_up_frames[] = {
    {&jump_image, 120}, {&spark_image, 180}, {&land_image, 100}, {&blink_image, 120},
};
static const struct pet_frame greeting_frames[] = {
    {&typing_image, 200}, {&jump_image, 180}, {&idle_image, 600}, {&blink_image, 130},
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
static const lv_image_dsc_t *const typing_effects[] = {
    &puff_0_image, &puff_1_image, &puff_2_image, &puff_pop_image,
};
static const lv_image_dsc_t *const celebration_effects[] = {
    &confetti_0_image, &confetti_1_image, &confetti_2_image, &confetti_3_image,
};
static const lv_image_dsc_t *const greeting_effects[] = {
    &greeting_0_image, &greeting_1_image, &greeting_2_image, &greeting_0_image,
};
BUILD_ASSERT(ARRAY_SIZE(typing_effects) == ARRAY_SIZE(typing_frames));
BUILD_ASSERT(ARRAY_SIZE(celebration_effects) == ARRAY_SIZE(level_up_frames));
BUILD_ASSERT(ARRAY_SIZE(greeting_effects) == ARRAY_SIZE(greeting_frames));

#define ANIMATION(frames, effects, x, y) {frames, ARRAY_SIZE(frames), effects, x, y}
static const struct pet_animation animations[] = {
    [PET_TYPING] = ANIMATION(typing_frames, typing_effects, 10, 12),
    [PET_LEVEL_UP] = ANIMATION(level_up_frames, celebration_effects, 0, 0),
    [PET_POWERED] = ANIMATION(powered_frames, powered_effects, 10, 12),
    [PET_GREETING] = ANIMATION(greeting_frames, greeting_effects, 10, 12),
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
    if (pet_visible && headroom && state == PET_SLEEP) {
        lv_image_set_src(effect_image, &sleep_bubble_image);
        lv_obj_set_pos(effect_image, 10, 12);
        lv_obj_remove_flag(effect_image, LV_OBJ_FLAG_HIDDEN);
    } else if (pet_visible && headroom && animations[state].effects) {
        const struct pet_animation *animation = &animations[state];
        lv_image_set_src(effect_image, animation->effects[frame_index]);
        lv_obj_set_pos(effect_image, animation->effect_x, animation->effect_y);
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

/* Where an animation ends: asleep when the keyboard is idle, awake otherwise.
 * USB power and a hidden pet are handled by steno_squid_pet_set_state().
 */
static enum pet_state resting_state(void) {
    return zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE ? PET_IDLE : PET_SLEEP;
}

static void advance_frame(lv_timer_t *timer) {
    frame_index++;
    if (frame_index == animations[state].count) {
        if (state == PET_POWERED) {
            frame_index = 0;
            show_frame();
        } else {
            steno_squid_pet_set_state(resting_state());
        }
    } else {
        show_frame();
    }
}

void steno_squid_pet_create(lv_obj_t *parent) {
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

void steno_squid_pet_set_state(enum pet_state next) {
    if (!pet_image) {
        return;
    }

    if (!pet_visible) {
        next = PET_IDLE;
    } else if (usb_powered && (next == PET_IDLE || next == PET_SLEEP)) {
        next = PET_POWERED;
    }
    /* Finish the current puff before a later keypress starts another one. */
    if (next == PET_TYPING && state != PET_IDLE && state != PET_POWERED) {
        return;
    }
    if (next == PET_GREETING && state == PET_LEVEL_UP) {
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

void steno_squid_pet_set_visible(bool visible) {
    if (!pet_image || pet_visible == visible) {
        return;
    }
    pet_visible = visible;
    if (visible) {
        lv_obj_remove_flag(pet_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(pet_image, LV_OBJ_FLAG_HIDDEN);
    }
    steno_squid_pet_set_state(PET_IDLE);
}

void steno_squid_pet_set_powered(bool powered) {
    if (usb_powered == powered) {
        return;
    }
    usb_powered = powered;
    if (state == PET_IDLE || state == PET_SLEEP || state == PET_POWERED) {
        steno_squid_pet_set_state(PET_IDLE);
    }
}

void steno_squid_pet_set_headroom(bool available) {
    headroom = available;
    if (pet_image) {
        update_effect();
    }
}
