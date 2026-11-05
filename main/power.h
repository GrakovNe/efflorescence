/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On a battery: turns on automatic light sleep. Wi-Fi stays associated;
 * the chip dozes between beacons whenever nothing holds it awake.
 * Without one there is nothing to save and the chip stays awake. */
void power_start(bool battery);

/* Whether the chip may sleep at all. */
bool power_sleeps(void);

/* The LED needs the PWM clock, which light sleep stops. Called by the
 * lamp task only: true before the LED gets a duty, false once it is
 * dark again. */
void power_led_active(bool active);

/* Where the time went, as text: for the bench, there being no ammeter. */
size_t power_dump(char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
