/*
 * Behavior: pad_touch
 *
 * Receives cross-pad touch state from the central side via INVOKE_BEHAVIOR.
 * param1 = finger_count (0-3). Always invoked with state=true (pressed).
 * finger_count=0 means the peer has no fingers on its pad.
 */
#define DT_DRV_COMPAT zmk_behavior_pad_touch

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <iqs9151_cross_pad.h>

LOG_MODULE_DECLARE(iqs9151, CONFIG_INPUT_IQS9151_LOG_LEVEL);

static int on_binding_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    /*
     * Look up the local IQS9151 device and update its peer state.
     * This runs on the peripheral side.
     */
    const struct device *iqs_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(iqs9151));

    if (iqs_dev == NULL) {
        LOG_WRN("pad_touch: iqs9151 device not found");
        return -ENODEV;
    }

#ifdef CONFIG_INPUT_IQS9151_CROSS_PAD
    iqs9151_set_peer_state(iqs_dev, (uint8_t)binding->param1);
    LOG_DBG("pad_touch: peer fc=%d", binding->param1);
#endif
    return 0;
}

static int on_binding_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    /* Not used in this design (always state=true), but safe fallback. */
    const struct device *iqs_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(iqs9151));

    if (iqs_dev == NULL) {
        return -ENODEV;
    }

#ifdef CONFIG_INPUT_IQS9151_CROSS_PAD
    iqs9151_set_peer_state(iqs_dev, 0);
#endif
    return 0;
}

static const struct behavior_driver_api pad_touch_driver_api = {
    .binding_pressed = on_binding_pressed,
    .binding_released = on_binding_released,
};

#define PAD_TOUCH_INST(n)                                                      \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
                            &pad_touch_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PAD_TOUCH_INST)
