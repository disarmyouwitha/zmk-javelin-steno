#define DT_DRV_COMPAT zmk_behavior_javelin_practice

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "../zen_r4/split_status.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int practice_key_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    zen_r4_practice_toggle();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int practice_key_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api practice_key_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = practice_key_pressed,
    .binding_released = practice_key_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &practice_key_api);

#endif // DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
