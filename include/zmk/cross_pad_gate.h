#pragma once

#include <stdbool.h>

/**
 * Set the cross-pad gate state.
 * When active, the gate input processor suppresses REL events from the
 * peripheral proxy device.
 */
void zmk_cross_pad_gate_set(bool active);

/**
 * Get the current cross-pad gate state.
 */
bool zmk_cross_pad_gate_get(void);
