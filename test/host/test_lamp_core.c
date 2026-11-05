/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "lamp_core.h"

#include "tinytest.h"

/* L* = 20: about what a 2.9 % duty floor comes to. */
#define FLOOR 13107
#define MAX   ((int)LAMP_LEVEL_MAX)
#define MID   ((FLOOR + MAX) / 2)
#define LVL(pct) lamp_pct_to_level((pct), FLOOR)

static void test_pct(void)
{
    CHECK(LVL(1) == FLOOR);
    CHECK(LVL(100) == MAX);
    CHECK(LVL(0) == FLOOR);
    CHECK(LVL(200) == MAX);
    CHECK(lamp_level_to_pct(0, FLOOR) == LAMP_PCT_MIN);
    CHECK(lamp_level_to_pct(FLOOR, FLOOR) == LAMP_PCT_MIN);
    CHECK(lamp_level_to_pct(MAX, FLOOR) == 100);
    for (int pct = LAMP_PCT_MIN; pct <= LAMP_PCT_MAX; pct++) {
        CHECK(lamp_level_to_pct(LVL((uint8_t)pct), FLOOR) == pct);
    }
    /* Even steps in lightness between the floor and the top. */
    CHECK(LVL(51) - LVL(50) >= (MAX - FLOOR) / 99 - 1);
    CHECK(LVL(51) - LVL(50) <= (MAX - FLOOR) / 99 + 1);

    /* No floor: the scale starts at dark. */
    CHECK(lamp_pct_to_level(1, 0) == 0);
    CHECK(lamp_pct_to_level(100, 0) == MAX);
    CHECK(lamp_level_to_pct(MAX, MAX) == LAMP_PCT_MIN);
}

static void test_init(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 60, FLOOR);
    CHECK(c.on && c.brightness == 60);
    CHECK(c.level == LVL(60));
    CHECK(!lamp_core_busy(&c));

    lamp_core_init(&c, false, 60, FLOOR);
    CHECK(!c.on && c.brightness == 60 && c.level == 0);

    lamp_core_init(&c, true, 0, FLOOR);
    CHECK(c.brightness == LAMP_PCT_MIN && c.level == FLOOR);
}

static void test_set_jump(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 60, FLOOR);
    uint32_t seq = c.seq;

    lamp_core_set(&c, 1000, true, 0, 0);
    CHECK(c.on && c.level == LVL(60) && !lamp_core_busy(&c));
    CHECK(c.seq == seq + 1);

    lamp_core_set(&c, 1100, true, 0, 0);
    CHECK(c.seq == seq + 1);

    lamp_core_set(&c, 1200, true, 30, 0);
    CHECK(c.brightness == 30 && c.level == LVL(30));
    CHECK(c.seq == seq + 2);

    lamp_core_set(&c, 1300, false, 0, 0);
    CHECK(!c.on && c.level == 0 && c.brightness == 30);
    CHECK(c.seq == seq + 3);
}

/* A given time: straight through the stretch below the floor in
 * LAMP_TRANSIT_MS, then a straight line up to the brightness. */
static void test_fade_in(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100, FLOOR);
    lamp_core_set(&c, 0, true, 0, 1000);
    CHECK(c.on && c.level == 0 && lamp_core_busy(&c));

    lamp_core_tick(&c, LAMP_TRANSIT_MS / 2);
    CHECK(c.level >= FLOOR / 2 - 2 && c.level <= FLOOR / 2 + 2);
    lamp_core_tick(&c, LAMP_TRANSIT_MS);
    CHECK(c.level == FLOOR);

    int64_t rest = 1000 - LAMP_TRANSIT_MS;
    lamp_core_tick(&c, LAMP_TRANSIT_MS + rest / 4);
    int quarter = FLOOR + (MAX - FLOOR) / 4;
    CHECK(c.level >= quarter - 20 && c.level <= quarter + 20);
    lamp_core_tick(&c, LAMP_TRANSIT_MS + rest / 2);
    CHECK(c.level >= MID - 20 && c.level <= MID + 20);

    lamp_core_tick(&c, 999);
    CHECK(c.level < MAX && lamp_core_busy(&c));
    lamp_core_tick(&c, 1000);
    CHECK(c.level == MAX && !lamp_core_busy(&c));
}

static void test_fade_out(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 100, FLOOR);
    lamp_core_toggle(&c, 0, 1000);
    CHECK(!c.on && c.level == MAX && lamp_core_busy(&c));

    int64_t first = 1000 - LAMP_TRANSIT_MS;
    lamp_core_tick(&c, first / 2);
    CHECK(c.level >= MID - 20 && c.level <= MID + 20);
    lamp_core_tick(&c, first);
    CHECK(c.level == FLOOR);
    /* The last stretch is short and straight: no lingering at the bottom
     * and no step to dark. */
    lamp_core_tick(&c, first + LAMP_TRANSIT_MS / 2);
    CHECK(c.level >= FLOOR / 2 - 2 && c.level <= FLOOR / 2 + 2);
    lamp_core_tick(&c, 999);
    CHECK(c.level > 0 && c.level < FLOOR / 20 && lamp_core_busy(&c));
    lamp_core_tick(&c, 1000);
    CHECK(c.level == 0 && !c.on && !lamp_core_busy(&c));
}

/* How long a soft switch takes from this brightness, and how far the
 * light has moved a tenth of the way in. */
static int soft_off(uint8_t pct, int *moved_early_pct)
{
    lamp_core_t c;
    lamp_core_init(&c, true, pct, FLOOR);
    int from = c.level;
    lamp_core_toggle(&c, 0, LAMP_TRANSITION_SOFT);
    int t = 0;
    int total = 0;
    while (lamp_core_busy(&c) && t < 5000) {
        t++;
        lamp_core_tick(&c, t);
        total = t;
    }
    lamp_core_init(&c, true, pct, FLOOR);
    lamp_core_toggle(&c, 0, LAMP_TRANSITION_SOFT);
    lamp_core_tick(&c, total / 10);
    *moved_early_pct = (from - c.level) * 100 / from;
    return total;
}

/* The soft switch off: the light starts down at once, at a steady pace,
 * and a dim lamp is not kept waiting. */
static void test_soft_off(void)
{
    int early;
    int full = soft_off(100, &early);
    CHECK(full >= LAMP_SOFT_MS + LAMP_TRANSIT_MS - 2 && full <= LAMP_SOFT_MS + LAMP_TRANSIT_MS + 2);
    /* A tenth of the time in, a fair part of the way down already. */
    CHECK(early >= 7);

    int half = soft_off(50, &early);
    CHECK(half > full / 2 - 20 && half < full / 2 + 60);
    CHECK(early >= 5);

    /* At the dimmest there is only the short stretch to dark. */
    int dim = soft_off(1, &early);
    CHECK(dim >= LAMP_TRANSIT_MS - 2 && dim <= LAMP_TRANSIT_MS + 2);
    CHECK(early >= 7);
    /* Just above it: a few milliseconds more, and it is already moving
     * (slower than it will below the floor, hence the smaller share). */
    int low = soft_off(3, &early);
    CHECK(low < LAMP_TRANSIT_MS + 30);
    CHECK(early >= 4);

    /* Never a pause followed by a drop: no millisecond takes more than
     * its share by far. */
    lamp_core_t c;
    for (int pct = 1; pct <= 100; pct += 3) {
        lamp_core_init(&c, true, (uint8_t)pct, FLOOR);
        int prev = c.level;
        int max_step = 0;
        int still = 0;
        int longest_still = 0;
        lamp_core_toggle(&c, 0, LAMP_TRANSITION_SOFT);
        for (int t = 1; lamp_core_busy(&c) && t < 5000; t++) {
            lamp_core_tick(&c, t);
            int step = prev - c.level;
            CHECK(step >= 0);
            if (step > max_step) {
                max_step = step;
            }
            still = step == 0 ? still + 1 : 0;
            if (still > longest_still) {
                longest_still = still;
            }
            prev = c.level;
        }
        CHECK(c.level == 0 && !c.on);
        CHECK(max_step <= FLOOR / LAMP_TRANSIT_MS + 2);
        CHECK(longest_still <= 2);
    }
}

/* The soft switch on: quick out of the dark, then slowing into the
 * brightness. */
static void test_soft_on(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100, FLOOR);
    lamp_core_toggle(&c, 0, LAMP_TRANSITION_SOFT);
    lamp_core_tick(&c, LAMP_TRANSIT_MS);
    CHECK(c.level == FLOOR);

    /* Half of the remaining time covers three quarters of the way. */
    lamp_core_tick(&c, LAMP_TRANSIT_MS + LAMP_SOFT_MS / 2);
    int three_quarters = FLOOR + (MAX - FLOOR) * 3 / 4;
    CHECK(c.level >= three_quarters - 30 && c.level <= three_quarters + 30);

    int prev = 0;
    int last_step = 0;
    lamp_core_init(&c, false, 100, FLOOR);
    lamp_core_toggle(&c, 0, LAMP_TRANSITION_SOFT);
    for (int t = 1; t <= LAMP_TRANSIT_MS + LAMP_SOFT_MS; t++) {
        lamp_core_tick(&c, t);
        CHECK(c.level >= prev);
        last_step = c.level - prev;
        prev = c.level;
    }
    CHECK(c.level == MAX && !lamp_core_busy(&c));
    /* It lands softly. */
    CHECK(last_step <= 2);

    /* To a dim brightness it is over almost as soon as it is lit. */
    lamp_core_init(&c, false, 3, FLOOR);
    lamp_core_toggle(&c, 0, LAMP_TRANSITION_SOFT);
    lamp_core_tick(&c, LAMP_TRANSIT_MS + 30);
    CHECK(c.level == LVL(3) && !lamp_core_busy(&c));
}

/* The soft switch between two brightnesses takes its share of the time. */
static void test_soft_change(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 20, FLOOR);
    lamp_core_set(&c, 0, true, 80, LAMP_TRANSITION_SOFT);
    int expect = LAMP_SOFT_MS * (LVL(80) - LVL(20)) / (MAX - FLOOR);
    lamp_core_tick(&c, expect - 5);
    CHECK(lamp_core_busy(&c));
    lamp_core_tick(&c, expect + 1);
    CHECK(c.level == LVL(80) && !lamp_core_busy(&c));

    lamp_core_set(&c, 5000, true, 80, LAMP_TRANSITION_SOFT);
    CHECK(!lamp_core_busy(&c));
}

static void test_reverse_mid_fade(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 100, FLOOR);
    lamp_core_toggle(&c, 0, 1000);
    lamp_core_tick(&c, 300);
    uint16_t at = c.level;
    CHECK(at < MAX && at > MID);

    /* A tap in the middle of the fade turns it around from where it is. */
    lamp_core_toggle(&c, 300, 1000);
    CHECK(c.on && c.level == at);
    lamp_core_tick(&c, 800);
    CHECK(c.level > at && c.level < MAX);
    lamp_core_tick(&c, 1300);
    CHECK(c.level == MAX && !lamp_core_busy(&c));

    /* Turned around while already below the floor. */
    lamp_core_toggle(&c, 2000, 1000);
    lamp_core_tick(&c, 2000 + 1000 - LAMP_TRANSIT_MS / 2);
    CHECK(c.level < FLOOR && c.level > 0);
    lamp_core_toggle(&c, 2000 + 1000 - LAMP_TRANSIT_MS / 2, 1000);
    lamp_core_tick(&c, 3200);
    CHECK(c.level > FLOOR);
    lamp_core_tick(&c, 4000);
    CHECK(c.level == MAX && !lamp_core_busy(&c));
}

/* A change of brightness while on: a straight line, nothing below the
 * floor to cross. */
static void test_brightness_change_is_linear(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 20, FLOOR);
    lamp_core_set(&c, 0, true, 80, 1000);
    lamp_core_tick(&c, 250);
    int quarter = LVL(20) + (LVL(80) - LVL(20)) / 4;
    CHECK(c.level >= quarter - 2 && c.level <= quarter + 2);
    lamp_core_tick(&c, 1000);
    CHECK(c.level == LVL(80) && !lamp_core_busy(&c));
}

/* A 30-minute sunrise spends no more than a blink below the floor. */
static void test_sunrise(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 80, FLOOR);
    uint32_t total = 30 * 60 * 1000;
    lamp_core_set(&c, 0, true, 0, total);
    lamp_core_tick(&c, LAMP_TRANSIT_MS);
    CHECK(c.level == FLOOR);

    uint16_t prev = FLOOR;
    for (int64_t t = 1000; t <= total; t += 10000) {
        lamp_core_tick(&c, t);
        CHECK(c.level >= prev);
        prev = c.level;
    }
    lamp_core_tick(&c, total / 2 + LAMP_TRANSIT_MS / 2);
    lamp_core_tick(&c, total);
    CHECK(c.level == LVL(80) && !lamp_core_busy(&c));
}

static void test_short_fade_keeps_most_time_above_floor(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100, FLOOR);
    /* 90 ms in all: the stretch below the floor gets a third, not 80. */
    lamp_core_set(&c, 0, true, 0, 90);
    lamp_core_tick(&c, 30);
    CHECK(c.level == FLOOR);
    lamp_core_tick(&c, 90);
    CHECK(c.level == MAX);
}

static void test_hold_ramp(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 50, FLOOR);
    uint16_t start = c.level;

    lamp_core_hold_begin(&c, 0);
    CHECK(lamp_core_busy(&c));
    lamp_core_tick(&c, 300);
    CHECK(c.level > start);
    CHECK(c.brightness == lamp_level_to_pct(c.level, FLOOR));

    lamp_core_tick(&c, LAMP_RAMP_MS);
    CHECK(c.level == MAX && c.brightness == 100);
    lamp_core_tick(&c, LAMP_RAMP_MS + 5000);
    CHECK(c.level == MAX);

    lamp_core_hold_end(&c, LAMP_RAMP_MS + 5000);
    CHECK(!lamp_core_busy(&c) && c.on && c.brightness == 100);

    /* The next hold goes the other way, takes LAMP_RAMP_MS for the whole
     * range and stops at the floor, not at off. */
    lamp_core_hold_begin(&c, 10000);
    lamp_core_tick(&c, 10000 + LAMP_RAMP_MS / 2);
    CHECK(c.level >= MID - 20 && c.level <= MID + 20);
    lamp_core_tick(&c, 10000 + LAMP_RAMP_MS);
    CHECK(c.level == FLOOR && c.brightness == LAMP_PCT_MIN && c.on);
    lamp_core_tick(&c, 10000 + 2 * LAMP_RAMP_MS);
    CHECK(c.level == FLOOR);
    lamp_core_hold_end(&c, 10000 + 2 * LAMP_RAMP_MS);

    /* At the floor the only way is up, whatever the last direction was. */
    lamp_core_hold_begin(&c, 30000);
    lamp_core_hold_end(&c, 30010);
    lamp_core_hold_begin(&c, 31000);
    lamp_core_hold_end(&c, 31010);
    lamp_core_hold_begin(&c, 32000);
    lamp_core_tick(&c, 32500);
    CHECK(c.level > FLOOR);
    lamp_core_hold_end(&c, 32500);
}

static void test_hold_alternates(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 50, FLOOR);
    uint16_t mid = c.level;

    lamp_core_hold_begin(&c, 0);
    lamp_core_hold_end(&c, 500);
    uint16_t up = c.level;
    CHECK(up > mid);

    lamp_core_hold_begin(&c, 1000);
    lamp_core_hold_end(&c, 1500);
    CHECK(c.level < up);
}

static void test_hold_from_off(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 80, FLOOR);
    uint32_t seq = c.seq;

    lamp_core_hold_begin(&c, 0);
    CHECK(c.on && c.level == FLOOR && c.seq == seq + 1);
    lamp_core_tick(&c, 1500);
    CHECK(c.level > FLOOR && c.level < MAX);
    lamp_core_hold_end(&c, 1500);
    CHECK(c.on && c.brightness == lamp_level_to_pct(c.level, FLOOR));
}

static void test_hold_takes_over_fade(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100, FLOOR);
    lamp_core_set(&c, 0, true, 0, 60000);
    lamp_core_tick(&c, 30000);
    uint16_t half = c.level;

    lamp_core_hold_begin(&c, 30000);
    lamp_core_tick(&c, 30300);
    CHECK(c.level > half);
    lamp_core_hold_end(&c, 30300);
    uint16_t kept = c.level;

    lamp_core_tick(&c, 60000);
    CHECK(c.level == kept && !lamp_core_busy(&c));
}

static void test_set_ends_hold(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 50, FLOOR);
    lamp_core_hold_begin(&c, 0);
    lamp_core_set(&c, 100, true, 20, 0);
    lamp_core_tick(&c, 1000);
    CHECK(c.level == LVL(20) && c.brightness == 20);
    lamp_core_hold_end(&c, 1100);
    CHECK(c.level == LVL(20));
}

static void test_signal(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 30, FLOOR);
    uint16_t steady = c.level;
    CHECK(lamp_core_output(&c, 0) == steady);

    lamp_core_signal(&c, 1000, LAMP_SIGNAL_RESET_ARMED);
    CHECK(lamp_core_busy(&c));
    CHECK(lamp_core_output(&c, 1000) == LAMP_LEVEL_MAX / 2);
    CHECK(lamp_core_output(&c, 1249) == LAMP_LEVEL_MAX / 2);
    CHECK(lamp_core_output(&c, 1250) == 0);
    CHECK(lamp_core_output(&c, 1500) == LAMP_LEVEL_MAX / 2);

    lamp_core_signal(&c, 6000, LAMP_SIGNAL_RESET_READY);
    CHECK(lamp_core_output(&c, 6000) == LAMP_LEVEL_MAX / 2);
    CHECK(lamp_core_output(&c, 6060) == 0);
    CHECK(lamp_core_output(&c, 6120) == LAMP_LEVEL_MAX / 2);

    lamp_core_signal(&c, 7000, LAMP_SIGNAL_NONE);
    CHECK(!lamp_core_busy(&c));
    CHECK(lamp_core_output(&c, 7000) == steady && c.on && c.brightness == 30);
}

int main(void)
{
    test_pct();
    test_init();
    test_set_jump();
    test_fade_in();
    test_fade_out();
    test_soft_off();
    test_soft_on();
    test_soft_change();
    test_reverse_mid_fade();
    test_brightness_change_is_linear();
    test_sunrise();
    test_short_fade_keeps_most_time_above_floor();
    test_hold_ramp();
    test_hold_alternates();
    test_hold_from_off();
    test_hold_takes_over_fade();
    test_set_ends_hold();
    test_signal();
    REPORT();
}
