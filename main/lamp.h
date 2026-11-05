/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "appconf.h"
#include "lamp_core.h"
#include "light_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     on;
    uint8_t  brightness;
    uint16_t level;
    uint32_t seq;
    bool     soft;

    uint32_t duty;
    uint32_t duty_min;
    uint32_t duty_max;
    uint32_t freq_hz;
    bool     raw; /* the bench override drives the LED, not the state */
} lamp_status_t;

/* Not const: a soft on/off change is stored in cfg. */
void lamp_start(appconf_t *cfg);

void lamp_apply(const light_cmd_t *cmd);

void lamp_toggle(void);

void lamp_hold_begin(void);

void lamp_hold_end(void);

void lamp_signal(lamp_signal_t signal);

void lamp_get(lamp_status_t *out);

/* Saves a pending state change now, for use right before a restart. */
void lamp_flush(void);

/* Bench tool: put a raw duty on the LED and, with freq_hz != 0, change
 * the PWM frequency. duty < 0 hands the LED back to the state and
 * restores the configured frequency. */
void lamp_debug_pwm(int32_t duty, uint32_t freq_hz);

#ifdef __cplusplus
}
#endif
