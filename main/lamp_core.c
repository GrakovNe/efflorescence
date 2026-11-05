/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "lamp_core.h"

#include <string.h>

#define ARMED_PERIOD_MS 500
#define READY_PERIOD_MS 120
#define SIGNAL_LEVEL    (LAMP_LEVEL_MAX / 2)

static uint8_t clamp_pct(uint8_t pct)
{
    if (pct < LAMP_PCT_MIN) {
        return LAMP_PCT_MIN;
    }
    if (pct > LAMP_PCT_MAX) {
        return LAMP_PCT_MAX;
    }
    return pct;
}

uint16_t lamp_pct_to_level(uint8_t pct)
{
    pct = clamp_pct(pct);
    return (uint16_t)(((uint32_t)pct * LAMP_LEVEL_MAX + 50) / 100);
}

uint8_t lamp_level_to_pct(uint16_t level)
{
    uint32_t pct = ((uint32_t)level * 100 + LAMP_LEVEL_MAX / 2) / LAMP_LEVEL_MAX;
    return clamp_pct((uint8_t)pct);
}

void lamp_core_init(lamp_core_t *c, bool on, uint8_t brightness)
{
    memset(c, 0, sizeof *c);
    c->on = on;
    c->brightness = clamp_pct(brightness);
    c->level = on ? lamp_pct_to_level(c->brightness) : 0;
    c->ramp_dir = 1;
}

static void fade_to(lamp_core_t *c, int64_t now_ms, uint16_t target,
                    uint32_t transition_ms)
{
    if (transition_ms == 0 || c->level == target) {
        c->level = target;
        c->fading = false;
        return;
    }
    c->fading = true;
    c->fade_from = c->level;
    c->fade_to = target;
    c->fade_start_ms = now_ms;
    c->fade_ms = transition_ms;
}

void lamp_core_set(lamp_core_t *c, int64_t now_ms, bool on, uint8_t brightness,
                   uint32_t transition_ms)
{
    lamp_core_tick(c, now_ms);
    c->ramping = false;

    bool changed = false;
    if (brightness != 0) {
        brightness = clamp_pct(brightness);
        if (c->brightness != brightness) {
            c->brightness = brightness;
            changed = true;
        }
    }
    if (c->on != on) {
        c->on = on;
        changed = true;
    }
    if (changed) {
        c->seq++;
    }
    fade_to(c, now_ms, on ? lamp_pct_to_level(c->brightness) : 0,
            transition_ms);
}

void lamp_core_toggle(lamp_core_t *c, int64_t now_ms, uint32_t transition_ms)
{
    lamp_core_set(c, now_ms, !c->on, 0, transition_ms);
}

void lamp_core_hold_begin(lamp_core_t *c, int64_t now_ms)
{
    lamp_core_tick(c, now_ms);
    c->fading = false;

    uint16_t floor = lamp_pct_to_level(LAMP_PCT_MIN);
    if (!c->on) {
        c->on = true;
        c->seq++;
        c->ramp_dir = 1;
    } else if (c->level >= LAMP_LEVEL_MAX) {
        c->ramp_dir = -1;
    } else if (c->level <= floor) {
        c->ramp_dir = 1;
    }
    if (c->level < floor) {
        c->level = floor;
    }
    c->ramping = true;
    c->ramp_from = c->level;
    c->ramp_start_ms = now_ms;
}

void lamp_core_hold_end(lamp_core_t *c, int64_t now_ms)
{
    if (!c->ramping) {
        return;
    }
    lamp_core_tick(c, now_ms);
    c->ramping = false;
    c->ramp_dir = (int8_t)-c->ramp_dir;
}

void lamp_core_signal(lamp_core_t *c, int64_t now_ms, lamp_signal_t signal)
{
    c->signal = signal;
    c->signal_start_ms = now_ms;
}

void lamp_core_tick(lamp_core_t *c, int64_t now_ms)
{
    if (c->ramping) {
        int64_t elapsed = now_ms - c->ramp_start_ms;
        if (elapsed < 0) {
            elapsed = 0;
        }
        int64_t level = c->ramp_from +
                        c->ramp_dir * (elapsed * LAMP_LEVEL_MAX / LAMP_RAMP_MS);
        int64_t floor = lamp_pct_to_level(LAMP_PCT_MIN);
        if (level < floor) {
            level = floor;
        }
        if (level > LAMP_LEVEL_MAX) {
            level = LAMP_LEVEL_MAX;
        }
        c->level = (uint16_t)level;
        uint8_t pct = lamp_level_to_pct(c->level);
        if (pct != c->brightness) {
            c->brightness = pct;
            c->seq++;
        }
        return;
    }
    if (c->fading) {
        int64_t elapsed = now_ms - c->fade_start_ms;
        if (elapsed >= (int64_t)c->fade_ms) {
            c->level = c->fade_to;
            c->fading = false;
        } else if (elapsed > 0) {
            int64_t delta = (int64_t)c->fade_to - c->fade_from;
            c->level = (uint16_t)(c->fade_from + delta * elapsed / c->fade_ms);
        }
    }
}

bool lamp_core_busy(const lamp_core_t *c)
{
    return c->ramping || c->fading || c->signal != LAMP_SIGNAL_NONE;
}

uint16_t lamp_core_output(const lamp_core_t *c, int64_t now_ms)
{
    int64_t period;
    switch (c->signal) {
    case LAMP_SIGNAL_RESET_ARMED:
        period = ARMED_PERIOD_MS;
        break;
    case LAMP_SIGNAL_RESET_READY:
        period = READY_PERIOD_MS;
        break;
    default:
        return c->level;
    }
    int64_t phase = (now_ms - c->signal_start_ms) % period;
    return phase < period / 2 ? SIGNAL_LEVEL : 0;
}
