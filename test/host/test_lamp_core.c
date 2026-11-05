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

/* Turning on: straight through the stretch below the floor in
 * LAMP_TRANSIT_MS, then an S-curve up to the brightness. */
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
    /* Slow at both ends, half way at half time. */
    lamp_core_tick(&c, LAMP_TRANSIT_MS + rest / 10);
    CHECK(c.level - FLOOR < (MAX - FLOOR) / 30);
    lamp_core_tick(&c, LAMP_TRANSIT_MS + rest / 2);
    CHECK(c.level >= MID - 20 && c.level <= MID + 20);
    lamp_core_tick(&c, LAMP_TRANSIT_MS + rest * 9 / 10);
    CHECK(MAX - c.level < (MAX - FLOOR) / 30);

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

static void test_fade_is_monotonic_and_smooth(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100, FLOOR);
    lamp_core_set(&c, 0, true, 0, LAMP_SOFT_MS);
    uint16_t prev = 0;
    int max_step = 0;
    for (int t = 0; t <= LAMP_SOFT_MS; t++) {
        lamp_core_tick(&c, t);
        CHECK(c.level >= prev);
        if (c.level - prev > max_step) {
            max_step = c.level - prev;
        }
        prev = c.level;
    }
    CHECK(c.level == MAX);
    /* No millisecond moves the light by more than 0.4 % of the scale. */
    CHECK(max_step < MAX / 250);

    lamp_core_toggle(&c, 10000, LAMP_SOFT_MS);
    prev = MAX;
    for (int t = 0; t <= LAMP_SOFT_MS; t++) {
        lamp_core_tick(&c, 10000 + t);
        CHECK(c.level <= prev);
        prev = c.level;
    }
    CHECK(c.level == 0);
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
    test_fade_is_monotonic_and_smooth();
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
