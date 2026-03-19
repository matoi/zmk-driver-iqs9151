/*
 * Public API for cross-pad gesture communication.
 * Called by pad_touch behavior (peripheral) and input listener (central).
 */
#ifndef IQS9151_CROSS_PAD_H
#define IQS9151_CROSS_PAD_H

#include <zephyr/device.h>
#include <stdint.h>

#ifdef CONFIG_INPUT_IQS9151_CROSS_PAD

void iqs9151_set_peer_state(const struct device *dev, uint8_t finger_count);
void iqs9151_set_peer_rel_x(const struct device *dev, int16_t rel_x);

#endif /* CONFIG_INPUT_IQS9151_CROSS_PAD */
#endif /* IQS9151_CROSS_PAD_H */
