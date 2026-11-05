#define DT_DRV_COMPAT zmk_behavior_javelin_steno

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>

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
