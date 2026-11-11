#define DT_DRV_COMPAT zmk_behavior_javelin_steno

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/javelin_steno.h>

#include "zmk_javelin_steno/zmk_platform_shim.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if defined(CONFIG_ZMK_SPLIT) && !defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
// Bindings are forwarded to the central; peripherals do not link the engine.
void zmk_javelin_steno_init(void) {}
void zmk_javelin_steno_process_key(int key, bool pressed) {
    (void)key;
    (void)pressed;
}
#endif

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/* Lets ZMK Studio offer each steno key by name when assigning this behavior. */
#define STENO_KEY(_value, _name)                                                                   \
    { .display_name = _name, .value = _value, .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE }

static const struct behavior_parameter_value_metadata steno_keys[] = {
    STENO_KEY(STENO_S1, "S- (top)"),
    STENO_KEY(STENO_S2, "S- (bottom)"),
    STENO_KEY(STENO_TL, "T-"),
    STENO_KEY(STENO_KL, "K-"),
    STENO_KEY(STENO_PL, "P-"),
    STENO_KEY(STENO_WL, "W-"),
    STENO_KEY(STENO_HL, "H-"),
    STENO_KEY(STENO_RL, "R-"),
    STENO_KEY(STENO_A, "A"),
    STENO_KEY(STENO_O, "O"),
    STENO_KEY(STENO_STAR1, "* (left top)"),
    STENO_KEY(STENO_STAR2, "* (left bottom)"),
    STENO_KEY(STENO_STAR3, "* (right top)"),
    STENO_KEY(STENO_STAR4, "* (right bottom)"),
    STENO_KEY(STENO_E, "E"),
    STENO_KEY(STENO_U, "U"),
    STENO_KEY(STENO_FR, "-F"),
    STENO_KEY(STENO_RR, "-R"),
    STENO_KEY(STENO_PR, "-P"),
    STENO_KEY(STENO_BR, "-B"),
    STENO_KEY(STENO_LR, "-L"),
    STENO_KEY(STENO_GR, "-G"),
    STENO_KEY(STENO_TR, "-T"),
    STENO_KEY(STENO_SR, "-S"),
    STENO_KEY(STENO_DR, "-D"),
    STENO_KEY(STENO_ZR, "-Z"),
    STENO_KEY(STENO_NUM1, "Number bar 1"),
    STENO_KEY(STENO_NUM2, "Number bar 2"),
    STENO_KEY(STENO_NUM3, "Number bar 3"),
    STENO_KEY(STENO_NUM4, "Number bar 4"),
    STENO_KEY(STENO_NUM5, "Number bar 5"),
    STENO_KEY(STENO_NUM6, "Number bar 6"),
    STENO_KEY(STENO_NUM7, "Number bar 7"),
    STENO_KEY(STENO_NUM8, "Number bar 8"),
    STENO_KEY(STENO_NUM9, "Number bar 9"),
    STENO_KEY(STENO_NUM10, "Number bar 10"),
    STENO_KEY(STENO_NUM11, "Number bar 11"),
    STENO_KEY(STENO_NUM12, "Number bar 12"),
};

static const struct behavior_parameter_metadata_set steno_key_set = {
    .param1_values = steno_keys,
    .param1_values_len = ARRAY_SIZE(steno_keys),
};

static const struct behavior_parameter_metadata steno_metadata = {
    .sets_len = 1,
    .sets = &steno_key_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    zmk_javelin_steno_init();
    zmk_javelin_steno_process_key((int)binding->param1, true);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    zmk_javelin_steno_process_key((int)binding->param1, false);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_javelin_steno_init(const struct device *dev) {
    // Lazy initialization on first press, after C++ global constructors.
    return 0;
}

static const struct behavior_driver_api behavior_javelin_steno_driver_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &steno_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define JAVSTENO_INST(n)                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_javelin_steno_init, NULL,          \
                            NULL, NULL, POST_KERNEL,                        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,            \
                            &behavior_javelin_steno_driver_api);

DT_INST_FOREACH_STATUS_OKAY(JAVSTENO_INST)

int zmk_javelin_emit_keycode(uint16_t usage_page, uint32_t keycode, bool pressed) {
    return raise_zmk_keycode_state_changed((struct zmk_keycode_state_changed){
        .usage_page = usage_page, .keycode = keycode,
        .implicit_modifiers = 0, .explicit_modifiers = 0,
        .state = pressed, .timestamp = k_uptime_get(),
    });
}
