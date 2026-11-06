/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void touch_start(void);

/* Bench: log every move of the sensor output, not only the gestures. */
void touch_set_trace(bool on);
bool touch_trace(void);

/* Bench: cut the sensor's power for a moment so it calibrates again. */
void touch_reset_sensor(void);

/* How many times since boot the sensor stuck high and had its power
 * cycled for it. */
unsigned touch_stuck_count(void);

/* How many times since boot the sensor was made to calibrate again
 * because the light had come to rest at a new level. */
unsigned touch_recal_count(void);

#ifdef __cplusplus
}
#endif
