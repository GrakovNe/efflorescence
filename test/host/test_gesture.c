/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "gesture.h"

#include "tinytest.h"

#define STEP_MS  10
#define LOG_MAX  64

typedef struct {
    gesture_t g;
    int64_t now;
    gesture_event_t ev[LOG_MAX];
    uint8_t taps[LOG_MAX];
    int64_t at[LOG_MAX];
    int n;
} rig_t;

static void rig_init(rig_t *r)
{
    memset(r, 0, sizeof *r);
    gesture_init(&r->g);
    r->now = 1000;
}

/* Holds the sensor output at `touched` for `ms`, sampling every STEP_MS. */
static void run(rig_t *r, bool touched, int ms)
{
    for (int t = 0; t < ms; t += STEP_MS) {
        gesture_event_t e = gesture_feed(&r->g, touched, r->now);
        if (e != GESTURE_NONE && r->n < LOG_MAX) {
            r->ev[r->n] = e;
            r->taps[r->n] = r->g.taps;
            r->at[r->n] = r->now;
            r->n++;
        }
        r->now += STEP_MS;
    }
}

static void tap(rig_t *r)
{
    run(r, true, 100);
    run(r, false, 150);
}

static void test_tap(void)
{
    rig_t r;
    rig_init(&r);
    CHECK(!gesture_active(&r.g));

    run(&r, true, 100);
    CHECK(r.n == 0 && gesture_active(&r.g));
    run(&r, false, 100);
    CHECK(r.n == 1 && r.ev[0] == GESTURE_TAP && r.taps[0] == 1);
    CHECK(gesture_active(&r.g));

    run(&r, false, 600);
    CHECK(r.n == 2 && r.ev[1] == GESTURE_SERIES_END && r.taps[1] == 1);
    CHECK(!gesture_active(&r.g));
}

static void test_glitch_ignored(void)
{
    rig_t r;
    rig_init(&r);
    run(&r, true, 10);
    run(&r, false, 1000);
    CHECK(r.n == 0 && !gesture_active(&r.g));

    /* A dropout shorter than the debounce does not split a hold. */
    run(&r, true, 300);
    run(&r, false, 10);
    run(&r, true, 300);
    CHECK(r.n == 1 && r.ev[0] == GESTURE_HOLD_BEGIN);
}

static void test_hold(void)
{
    rig_t r;
    rig_init(&r);
    int64_t start = r.now;
    run(&r, true, 1000);
    CHECK(r.n == 1 && r.ev[0] == GESTURE_HOLD_BEGIN);
    CHECK(r.at[0] - start >= GESTURE_HOLD_MS);
    CHECK(r.at[0] - start <= GESTURE_HOLD_MS + STEP_MS);

    run(&r, false, 1000);
    CHECK(r.n == 2 && r.ev[1] == GESTURE_HOLD_END);
    CHECK(!gesture_active(&r.g));
}

static void test_series(void)
{
    rig_t r;
    rig_init(&r);
    tap(&r);
    tap(&r);
    tap(&r);
    CHECK(r.n == 3);
    CHECK(r.ev[0] == GESTURE_TAP && r.taps[0] == 1);
    CHECK(r.ev[1] == GESTURE_TAP && r.taps[1] == 2);
    CHECK(r.ev[2] == GESTURE_TAP && r.taps[2] == 3);

    run(&r, false, 600);
    CHECK(r.n == 4 && r.ev[3] == GESTURE_SERIES_END && r.taps[3] == 3);

    /* After the gap the count starts over. */
    tap(&r);
    CHECK(r.n == 5 && r.ev[4] == GESTURE_TAP && r.taps[4] == 1);
}

static void test_series_ends_before_late_press(void)
{
    rig_t r;
    rig_init(&r);
    tap(&r);
    run(&r, false, 400);
    CHECK(r.n == 2 && r.ev[1] == GESTURE_SERIES_END);
    run(&r, true, 100);
    run(&r, false, 100);
    CHECK(r.n == 3 && r.ev[2] == GESTURE_TAP && r.taps[2] == 1);
}

static void taps_then_hold(rig_t *r, int taps, int hold_ms)
{
    rig_init(r);
    for (int i = 0; i < taps; i++) {
        tap(r);
    }
    run(r, true, hold_ms);
    run(r, false, 100);
}

static void test_reset_fires(void)
{
    rig_t r;
    taps_then_hold(&r, GESTURE_RESET_TAPS, 6000);
    CHECK(r.n == GESTURE_RESET_TAPS + 3);
    for (int i = 0; i < GESTURE_RESET_TAPS; i++) {
        CHECK(r.ev[i] == GESTURE_TAP);
    }
    CHECK(r.ev[5] == GESTURE_RESET_ARMED);
    CHECK(r.ev[6] == GESTURE_RESET_READY);
    CHECK(r.at[6] - r.at[5] >= GESTURE_RESET_HOLD_MS - GESTURE_HOLD_MS - STEP_MS);
    CHECK(r.ev[7] == GESTURE_RESET_FIRE);

    run(&r, false, 1000);
    CHECK(r.n == GESTURE_RESET_TAPS + 3 && !gesture_active(&r.g));
}

static void test_reset_cancelled(void)
{
    rig_t r;
    taps_then_hold(&r, GESTURE_RESET_TAPS, 2000);
    CHECK(r.n == GESTURE_RESET_TAPS + 2);
    CHECK(r.ev[5] == GESTURE_RESET_ARMED);
    CHECK(r.ev[6] == GESTURE_RESET_CANCEL);

    /* The count does not survive a cancelled reset. */
    run(&r, true, 1000);
    CHECK(r.ev[7] == GESTURE_HOLD_BEGIN);
}

static void test_reset_needs_exact_count(void)
{
    rig_t r;
    taps_then_hold(&r, GESTURE_RESET_TAPS - 1, 6000);
    CHECK(r.ev[GESTURE_RESET_TAPS - 1] == GESTURE_HOLD_BEGIN);
    CHECK(r.ev[GESTURE_RESET_TAPS] == GESTURE_HOLD_END);

    taps_then_hold(&r, GESTURE_RESET_TAPS + 1, 6000);
    CHECK(r.ev[GESTURE_RESET_TAPS + 1] == GESTURE_HOLD_BEGIN);
    CHECK(r.ev[GESTURE_RESET_TAPS + 2] == GESTURE_HOLD_END);
}

static void test_reset_needs_unbroken_series(void)
{
    rig_t r;
    rig_init(&r);
    for (int i = 0; i < GESTURE_RESET_TAPS; i++) {
        tap(&r);
    }
    run(&r, false, 600);
    run(&r, true, 6000);
    run(&r, false, 100);
    CHECK(r.ev[r.n - 2] == GESTURE_HOLD_BEGIN);
    CHECK(r.ev[r.n - 1] == GESTURE_HOLD_END);
}

int main(void)
{
    test_tap();
    test_glitch_ignored();
    test_hold();
    test_series();
    test_series_ends_before_late_press();
    test_reset_fires();
    test_reset_cancelled();
    test_reset_needs_exact_count();
    test_reset_needs_unbroken_series();
    REPORT();
}
