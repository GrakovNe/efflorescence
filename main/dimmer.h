/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Perceived lightness, full scale: CIE L* = 100. */
#define DIMMER_LEVEL_MAX 65535u

/* PWM duty that gives a perceived lightness, by the CIE 1931 curve.
 * Level 0 is duty 0. */
uint32_t dimmer_duty(uint16_t level, uint32_t duty_max);

/* The lowest level whose duty is at least `duty`: where a floor given
 * as a duty sits on the lightness scale. */
uint16_t dimmer_level_for_duty(uint32_t duty, uint32_t duty_max);

#ifdef __cplusplus
}
#endif
