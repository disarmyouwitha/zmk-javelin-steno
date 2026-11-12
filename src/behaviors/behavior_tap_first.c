#define DT_DRV_COMPAT zmk_behavior_tap_first

#include <drivers/behavior.h>
#include <zephyr/device.h>

struct tap_first_config {
    const char *hold_tap;
};

static int forward(struct zmk_behavior_binding *binding,
                   struct zmk_behavior_binding_event event, bool pressed) {
    const struct device *device = zmk_behavior_get_binding(binding->behavior_dev);
    const struct tap_first_config *config = device->config;
    struct zmk_behavior_binding target = {
        .behavior_dev = config->hold_tap,
        .param1 = binding->param2,
        .param2 = binding->param1,
    };
    return zmk_behavior_invoke_binding(&target, event, pressed);
}

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return forward(binding, event, true);
}

static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return forward(binding, event, false);
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
struct tap_first_data {
    struct behavior_parameter_metadata_set parameters;
};

static int metadata(const struct device *device, struct behavior_parameter_metadata *result) {
    const struct tap_first_config *config = device->config;
    struct tap_first_data *data = device->data;
    struct behavior_parameter_metadata target;
    int err = behavior_get_parameter_metadata(zmk_behavior_get_binding(config->hold_tap), &target);
    if (err < 0) {
        return err;
    }
    if (target.sets_len != 1) {
        return -ENOTSUP;
    }
    data->parameters = (struct behavior_parameter_metadata_set){
        .param1_values = target.sets[0].param2_values,
        .param1_values_len = target.sets[0].param2_values_len,
        .param2_values = target.sets[0].param1_values,
        .param2_values_len = target.sets[0].param1_values_len,
    };
    result->sets = &data->parameters;
    result->sets_len = 1;
    return 0;
}
#endif

static const struct behavior_driver_api api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = pressed,
    .binding_released = released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = metadata,
#endif
};

#define TAP_FIRST_INST(n) \
    static const struct tap_first_config config_##n = { \
        .hold_tap = DEVICE_DT_NAME(DT_INST_PHANDLE(n, bindings)), \
    }; \
    IF_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA, (static struct tap_first_data data_##n;)) \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, \
        COND_CODE_1(CONFIG_ZMK_BEHAVIOR_METADATA, (&data_##n), (NULL)), \
        &config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(TAP_FIRST_INST)
