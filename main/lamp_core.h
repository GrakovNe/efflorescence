/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dimmer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAMP_LEVEL_MAX   DIMMER_LEVEL_MAX
#define LAMP_PCT_MIN     1
#define LAMP_PCT_MAX     100
#define LAMP_PCT_DEFAULT 60

#define LAMP_RAMP_MS 3000 /* a held finger sweeps the whole range in this time */
#define LAMP_SOFT_MS 700  /* soft on/off */

typedef enum {
    LAMP_SIGNAL_NONE,
    LAMP_SIGNAL_RESET_ARMED, /* slow blink: keep holding to reset */
    LAMP_SIGNAL_RESET_READY, /* fast blink: release to reset */
} lamp_signal_t;

typedef struct {
    bool     on;         /* what a tap toggles; false while fading out */
    uint8_t  brightness; /* percent, the level "on" settles at */
    uint16_t level;      /* what the LED shows now, 0 = dark */
    uint32_t seq;        /* bumped when on or brightness changes */

    bool     fading;
    uint16_t fade_from;
    uint16_t fade_to;
    int64_t  fade_start_ms;
    uint32_t fade_ms;

    bool     ramping;
    int8_t   ramp_dir;
    uint16_t ramp_from;
    int64_t  ramp_start_ms;

    lamp_signal_t signal;
    int64_t  signal_start_ms;
} lamp_core_t;

uint16_t lamp_pct_to_level(uint8_t pct);
uint8_t lamp_level_to_pct(uint16_t level);

void lamp_core_init(lamp_core_t *c, bool on, uint8_t brightness);

/* brightness 0 keeps the current one. transition_ms is the time for the
 * whole move from the present level, 0 for a jump. */
void lamp_core_set(lamp_core_t *c, int64_t now_ms, bool on, uint8_t brightness,
                   uint32_t transition_ms);

void lamp_core_toggle(lamp_core_t *c, int64_t now_ms, uint32_t transition_ms);

void lamp_core_hold_begin(lamp_core_t *c, int64_t now_ms);

void lamp_core_hold_end(lamp_core_t *c, int64_t now_ms);

void lamp_core_signal(lamp_core_t *c, int64_t now_ms, lamp_signal_t signal);

void lamp_core_tick(lamp_core_t *c, int64_t now_ms);

/* True while the level moves by itself and tick() has to be called. */
bool lamp_core_busy(const lamp_core_t *c);

/* The level to put on the LED: the state, or a signal blinking over it. */
uint16_t lamp_core_output(const lamp_core_t *c, int64_t now_ms);

#ifdef __cplusplus
}
#endif
