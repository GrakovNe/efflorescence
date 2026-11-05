/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The lamp cannot measure its battery. All it can tell is that the
 * supply collapsed: the chip's brownout detector resets it. */

/* This many brownouts in a row and the lamp stops trying. */
#define BATTERY_STREAK_LIMIT 3
/* Up this long without one: the row is over. */
#define BATTERY_SETTLE_S 60
/* The LED on this long without one: the battery is fine again. */
#define BATTERY_CLEAR_AFTER_S 300

typedef struct {
    bool     low;
    uint8_t  streak;
    uint32_t on_for_s;
} battery_state_t;

typedef enum {
    BATTERY_RST_POWER_ON, /* the supply was gone: nothing is known */
    BATTERY_RST_BROWNOUT,
    BATTERY_RST_OTHER,    /* restart, update, wake from deep sleep */
} battery_reset_t;

typedef enum {
    BATTERY_BOOT_NORMAL,
    BATTERY_BOOT_DARK,  /* come up with the LED off whatever was saved */
    BATTERY_BOOT_SLEEP, /* deep sleep until the sensor is touched */
} battery_boot_t;

battery_boot_t battery_on_boot(battery_state_t *s, battery_reset_t why);

/* Call every dt_s seconds. Returns true when `low` changed. */
bool battery_tick(battery_state_t *s, bool led_on, uint32_t uptime_s,
                  uint32_t dt_s);

/* False once there is nothing left to watch for. */
bool battery_watching(const battery_state_t *s);

#ifdef __cplusplus
}
#endif
