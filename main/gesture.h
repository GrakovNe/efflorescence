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

#define GESTURE_DEBOUNCE_MS   20
#define GESTURE_HOLD_MS       400  /* a longer press is a hold, a shorter one a tap */
#define GESTURE_SERIES_GAP_MS 500  /* taps closer than this make one series */
#define GESTURE_RESET_TAPS    5    /* exactly this many taps, then a hold */
#define GESTURE_RESET_HOLD_MS 5000

typedef enum {
    GESTURE_NONE,
    GESTURE_TAP,          /* on release; taps = number in the series so far */
    GESTURE_SERIES_END,   /* the series is over; taps = its length */
    GESTURE_HOLD_BEGIN,
    GESTURE_HOLD_END,
    GESTURE_RESET_ARMED,  /* the reset hold has started */
    GESTURE_RESET_READY,  /* held long enough: a release resets */
    GESTURE_RESET_CANCEL, /* released too early */
    GESTURE_RESET_FIRE,
} gesture_event_t;

typedef enum {
    GESTURE_PHASE_IDLE,
    GESTURE_PHASE_PRESSED,
    GESTURE_PHASE_HOLD,
    GESTURE_PHASE_RESET_ARMED,
    GESTURE_PHASE_RESET_READY,
} gesture_phase_t;

typedef struct {
    bool    raw;
    bool    stable;
    int64_t raw_since_ms;

    gesture_phase_t phase;
    int64_t press_ms;
    int64_t release_ms;
    uint8_t series;      /* taps in the open series */
    uint8_t taps;        /* the count that goes with the last event */
} gesture_t;

void gesture_init(gesture_t *g);

/* Call with every sample of the sensor output. At most one event per call. */
gesture_event_t gesture_feed(gesture_t *g, bool touched, int64_t now_ms);

/* False when nothing is pending and sampling can stop until the next edge. */
bool gesture_active(const gesture_t *g);

#ifdef __cplusplus
}
#endif
