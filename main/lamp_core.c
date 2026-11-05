/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "lamp_core.h"

#include <string.h>

#define ARMED_PERIOD_MS 500
#define READY_PERIOD_MS 120
#define SIGNAL_LEVEL    (LAMP_LEVEL_MAX / 2)

#define Q16 65536

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

uint16_t lamp_pct_to_level(uint8_t pct, uint16_t floor)
{
    pct = clamp_pct(pct);
    uint32_t span = LAMP_LEVEL_MAX - floor;
    uint32_t steps = LAMP_PCT_MAX - LAMP_PCT_MIN;
    return (uint16_t)(floor + (span * (pct - LAMP_PCT_MIN) + steps / 2) / steps);
}

uint8_t lamp_level_to_pct(uint16_t level, uint16_t floor)
{
    if (level <= floor || floor >= LAMP_LEVEL_MAX) {
        return LAMP_PCT_MIN;
    }
    uint32_t span = LAMP_LEVEL_MAX - floor;
    uint32_t steps = LAMP_PCT_MAX - LAMP_PCT_MIN;
    uint32_t pct = LAMP_PCT_MIN + ((uint32_t)(level - floor) * steps + span / 2) / span;
    return clamp_pct((uint8_t)pct);
}

void lamp_core_init(lamp_core_t *c, bool on, uint8_t brightness, uint16_t floor)
{
    memset(c, 0, sizeof *c);
    c->floor = floor;
    c->on = on;
    c->brightness = clamp_pct(brightness);
    c->level = on ? lamp_pct_to_level(c->brightness, floor) : 0;
    c->ramp_dir = 1;
}

/* x and the result are Q16 fractions: slow, fast, slow. */
static int64_t smoothstep(int64_t x)
{
    return x * x / Q16 * (3 * Q16 - 2 * x) / Q16;
}

/* A fade has up to two stretches. The one below the floor is crossed
 * straight and fast, wherever the fade starts or ends. The one above it
 * takes the rest of the time, along an S-curve or a straight line. */
typedef struct {
    int32_t lo_from, lo_to; /* the stretch below the floor */
    int32_t hi_from, hi_to; /* the stretch above it */
    int64_t lo_ms, hi_ms;
    bool    up;
} fade_plan_t;

static void fade_plan(const lamp_core_t *c, fade_plan_t *p)
{
    int32_t from = c->fade_from;
    int32_t to = c->fade_to;
    int32_t floor = c->floor;
    p->up = to > from;
    p->lo_from = from < floor ? from : floor;
    p->lo_to = to < floor ? to : floor;
    p->hi_from = from > floor ? from : floor;
    p->hi_to = to > floor ? to : floor;

    int32_t lo = p->lo_to > p->lo_from ? p->lo_to - p->lo_from
                                       : p->lo_from - p->lo_to;
    bool hi = p->hi_to != p->hi_from;
    p->lo_ms = 0;
    if (lo > 0) {
        p->lo_ms = (int64_t)LAMP_TRANSIT_MS * lo / floor;
        int64_t cap = hi ? c->fade_ms / 3 : c->fade_ms;
        if (p->lo_ms > cap) {
            p->lo_ms = cap;
        }
    }
    p->hi_ms = hi ? (int64_t)c->fade_ms - p->lo_ms : 0;
}

static uint16_t along_lo(const fade_plan_t *p, int64_t t)
{
    return (uint16_t)(p->lo_from + (int64_t)(p->lo_to - p->lo_from) * t / p->lo_ms);
}

static uint16_t along_hi(const lamp_core_t *c, const fade_plan_t *p, int64_t t)
{
    int64_t x = t * Q16 / p->hi_ms;
    if (c->fade_ease) {
        x = smoothstep(x);
    }
    return (uint16_t)(p->hi_from + (int64_t)(p->hi_to - p->hi_from) * x / Q16);
}

/* Returns false once the fade is over. */
static bool fade_at(const lamp_core_t *c, int64_t elapsed, uint16_t *level)
{
    fade_plan_t p;
    fade_plan(c, &p);
    if (elapsed >= p.lo_ms + p.hi_ms) {
        *level = c->fade_to;
        return false;
    }
    if (elapsed < 0) {
        elapsed = 0;
    }
    int64_t first_ms = p.up ? p.lo_ms : p.hi_ms;
    bool in_lo = p.up ? elapsed < first_ms : elapsed >= first_ms;
    int64_t t = elapsed < first_ms ? elapsed : elapsed - first_ms;
    *level = in_lo ? along_lo(&p, t) : along_hi(c, &p, t);
    return true;
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
    /* Coming out of the dark or going into it is what gets the S-curve. */
    c->fade_ease = target == 0 || c->level < c->floor;
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
    fade_to(c, now_ms, on ? lamp_pct_to_level(c->brightness, c->floor) : 0,
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

    if (!c->on) {
        c->on = true;
        c->seq++;
        c->ramp_dir = 1;
    } else if (c->level >= LAMP_LEVEL_MAX) {
        c->ramp_dir = -1;
    } else if (c->level <= c->floor) {
        c->ramp_dir = 1;
    }
    if (c->level < c->floor) {
        c->level = c->floor;
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
        int64_t span = LAMP_LEVEL_MAX - c->floor;
        int64_t level = c->ramp_from + c->ramp_dir * (elapsed * span / LAMP_RAMP_MS);
        if (level < c->floor) {
            level = c->floor;
        }
        if (level > LAMP_LEVEL_MAX) {
            level = LAMP_LEVEL_MAX;
        }
        c->level = (uint16_t)level;
        uint8_t pct = lamp_level_to_pct(c->level, c->floor);
        if (pct != c->brightness) {
            c->brightness = pct;
            c->seq++;
        }
        return;
    }
    if (c->fading) {
        c->fading = fade_at(c, now_ms - c->fade_start_ms, &c->level);
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
