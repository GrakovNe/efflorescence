/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lamp_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pass as transition_ms for the configured soft on/off. */
#define LAMP_TRANSITION_DEFAULT (-1)

typedef struct {
    bool     on;
    uint8_t  brightness;
    uint16_t level;
    uint32_t seq;
} lamp_status_t;

void lamp_start(void);

/* brightness 0 keeps the current one. */
void lamp_set(bool on, uint8_t brightness, int32_t transition_ms);

void lamp_toggle(void);

void lamp_hold_begin(void);

void lamp_hold_end(void);

void lamp_signal(lamp_signal_t signal);

void lamp_get(lamp_status_t *out);

#ifdef __cplusplus
}
#endif
