#define DT_DRV_COMPAT zmk_behavior_soft_off_led

#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/pm.h>

#include "status_led.h"

struct soft_off_led_config {
    uint32_t hold_ms;
};

struct soft_off_led_data {
    int64_t pressed_at;
    bool pressed;
};

static int soft_off_led_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct soft_off_led_config *config = dev->config;
    struct soft_off_led_data *data = dev->data;

    data->pressed_at = k_uptime_get();
    data->pressed = true;
    status_led_soft_off_start(config->hold_ms);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int soft_off_led_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct soft_off_led_config *config = dev->config;
    struct soft_off_led_data *data = dev->data;

    if (!data->pressed) {
        return ZMK_BEHAVIOR_OPAQUE;
    }
    data->pressed = false;
    bool shutting_down = k_uptime_get() - data->pressed_at >= config->hold_ms;
    status_led_soft_off_stop(shutting_down);
    if (shutting_down) {
        // Allow the split release event to reach the peripheral before shutting down.
        if (IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) {
            k_msleep(100);
        }
        int err = zmk_pm_soft_off();
        // A failed suspend must not leave normal LED indications suppressed.
        status_led_soft_off_stop(false);
        return err;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api soft_off_led_api = {
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
    .binding_pressed = soft_off_led_pressed,
    .binding_released = soft_off_led_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define SOFT_OFF_LED_INST(n)                                                                        \
    static const struct soft_off_led_config soft_off_led_config_##n = {                             \
        .hold_ms = DT_INST_PROP(n, hold_time_ms),                                                    \
    };                                                                                             \
    static struct soft_off_led_data soft_off_led_data_##n;                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &soft_off_led_data_##n, &soft_off_led_config_##n,          \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &soft_off_led_api);

DT_INST_FOREACH_STATUS_OKAY(SOFT_OFF_LED_INST)
