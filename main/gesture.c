/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "gesture.h"

#include <string.h>

void gesture_init(gesture_t *g)
{
    memset(g, 0, sizeof *g);
}

static gesture_event_t on_release(gesture_t *g)
{
    gesture_phase_t phase = g->phase;
    g->phase = GESTURE_PHASE_IDLE;
    g->release_ms = g->raw_since_ms;

    switch (phase) {
    case GESTURE_PHASE_PRESSED:
        if (g->series < UINT8_MAX) {
            g->series++;
        }
        g->taps = g->series;
        return GESTURE_TAP;
    case GESTURE_PHASE_HOLD:
        return GESTURE_HOLD_END;
    case GESTURE_PHASE_RESET_ARMED:
        return GESTURE_RESET_CANCEL;
    case GESTURE_PHASE_RESET_READY:
        return GESTURE_RESET_FIRE;
    case GESTURE_PHASE_STUCK:
        g->series = 0;
        return GESTURE_NONE;
    default:
        return GESTURE_NONE;
    }
}

gesture_event_t gesture_feed(gesture_t *g, bool touched, int64_t now_ms)
{
    if (touched != g->raw) {
        g->raw = touched;
        g->raw_since_ms = now_ms;
    }

    /* A press that began inside the gap continues the series even if it
     * is still being debounced. */
    if (g->phase == GESTURE_PHASE_IDLE && g->series > 0) {
        int64_t at = g->raw ? g->raw_since_ms : now_ms;
        if (at - g->release_ms >= GESTURE_SERIES_GAP_MS) {
            g->taps = g->series;
            g->series = 0;
            return GESTURE_SERIES_END;
        }
    }

    if (g->raw != g->stable &&
        now_ms - g->raw_since_ms >= GESTURE_DEBOUNCE_MS) {
        g->stable = g->raw;
        if (!g->stable) {
            return on_release(g);
        }
        g->phase = GESTURE_PHASE_PRESSED;
        g->press_ms = g->raw_since_ms;
    }

    int64_t held_ms = now_ms - g->press_ms;
    if (g->phase == GESTURE_PHASE_PRESSED && held_ms >= GESTURE_HOLD_MS) {
        bool reset = g->series == GESTURE_RESET_TAPS;
        g->series = 0;
        if (reset) {
            g->phase = GESTURE_PHASE_RESET_ARMED;
            return GESTURE_RESET_ARMED;
        }
        g->phase = GESTURE_PHASE_HOLD;
        return GESTURE_HOLD_BEGIN;
    }
    if (g->phase == GESTURE_PHASE_RESET_ARMED &&
        held_ms >= GESTURE_RESET_HOLD_MS) {
        g->phase = GESTURE_PHASE_RESET_READY;
        return GESTURE_RESET_READY;
    }
    if ((g->phase == GESTURE_PHASE_HOLD || g->phase == GESTURE_PHASE_STUCK) &&
        held_ms >= GESTURE_STUCK_MS) {
        g->phase = GESTURE_PHASE_STUCK;
        /* Count the next round from here. */
        g->press_ms = now_ms;
        return GESTURE_STUCK;
    }
    return GESTURE_NONE;
}

bool gesture_active(const gesture_t *g)
{
    return g->raw || g->stable || g->phase != GESTURE_PHASE_IDLE ||
           g->series > 0;
}
