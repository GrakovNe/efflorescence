/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Perceptual brightness, full scale. */
#define DIMMER_LEVEL_MAX 65535u

/* PWM duty for a perceptual level, CIE 1931 lightness curve. Level 0 is
 * off (duty 0); every other level lands in duty_min..duty_max. */
uint32_t dimmer_duty(uint16_t level, uint32_t duty_min, uint32_t duty_max);

#ifdef __cplusplus
}
#endif
