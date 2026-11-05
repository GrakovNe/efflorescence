/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Looks at why the chip started. After a row of brownouts it does not
 * return: the lamp goes into deep sleep until the sensor is touched.
 * Runs before anything else drives a pin or starts the radio. */
void battery_early_init(void);

/* Starts watching for the flag to clear. After lamp_start(). */
void battery_start(void);

bool battery_low(void);

/* This start follows a brownout: the LED stays off whatever was saved. */
bool battery_boot_dark(void);

/* Bench tool, there being no way to drain the battery on request:
 * "brownout" restarts as if the supply had collapsed, "sleep" does it as
 * the last of a row, "clear" drops the flag. False for anything else. */
bool battery_simulate(const char *what);

#ifdef __cplusplus
}
#endif
