/*
 * Cross-pad gate input processor.
 *
 * When the cross-pad gesture is active, suppresses REL events from the
 * peripheral proxy device so that leaked normal cursor processing during
 * the BLE round-trip delay does not produce unwanted cursor movement.
 */

#define DT_DRV_COMPAT zmk_input_processor_cross_pad_gate

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static atomic_t cross_pad_gate_active = ATOMIC_INIT(0);

void zmk_cross_pad_gate_set(bool active) {
    atomic_set(&cross_pad_gate_active, active ? 1 : 0);
}

bool zmk_cross_pad_gate_get(void) {
    return atomic_get(&cross_pad_gate_active) != 0;
}

static int cross_pad_gate_handle_event(const struct device *dev,
                                       struct input_event *event,
                                       uint32_t param1, uint32_t param2,
                                       struct zmk_input_processor_state *state) {
    ARG_UNUSED(dev);
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (atomic_get(&cross_pad_gate_active) && event->type == INPUT_EV_REL) {
        LOG_DBG("cross-pad gate: suppressed REL code=%d value=%d",
                event->code, event->value);
        return ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api cross_pad_gate_driver_api = {
    .handle_event = cross_pad_gate_handle_event,
};

#define CROSS_PAD_GATE_INST(n)                                                 \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                 \
                          &cross_pad_gate_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CROSS_PAD_GATE_INST)
