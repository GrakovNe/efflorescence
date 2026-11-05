/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "lamp_core.h"

#include "tinytest.h"

#define FLOOR lamp_pct_to_level(LAMP_PCT_MIN)

static void test_pct(void)
{
    CHECK(lamp_pct_to_level(100) == LAMP_LEVEL_MAX);
    CHECK(lamp_pct_to_level(0) == lamp_pct_to_level(1));
    CHECK(lamp_pct_to_level(200) == LAMP_LEVEL_MAX);
    CHECK(lamp_level_to_pct(0) == LAMP_PCT_MIN);
    CHECK(lamp_level_to_pct(LAMP_LEVEL_MAX) == 100);
    for (int pct = LAMP_PCT_MIN; pct <= LAMP_PCT_MAX; pct++) {
        CHECK(lamp_level_to_pct(lamp_pct_to_level((uint8_t)pct)) == pct);
    }
}

static void test_init(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 60);
    CHECK(c.on && c.brightness == 60);
    CHECK(c.level == lamp_pct_to_level(60));
    CHECK(!lamp_core_busy(&c));

    lamp_core_init(&c, false, 60);
    CHECK(!c.on && c.brightness == 60 && c.level == 0);

    lamp_core_init(&c, true, 0);
    CHECK(c.brightness == LAMP_PCT_MIN);
}

static void test_set_jump(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 60);
    uint32_t seq = c.seq;

    lamp_core_set(&c, 1000, true, 0, 0);
    CHECK(c.on && c.level == lamp_pct_to_level(60) && !lamp_core_busy(&c));
    CHECK(c.seq == seq + 1);

    lamp_core_set(&c, 1100, true, 0, 0);
    CHECK(c.seq == seq + 1);

    lamp_core_set(&c, 1200, true, 30, 0);
    CHECK(c.brightness == 30 && c.level == lamp_pct_to_level(30));
    CHECK(c.seq == seq + 2);

    lamp_core_set(&c, 1300, false, 0, 0);
    CHECK(!c.on && c.level == 0 && c.brightness == 30);
    CHECK(c.seq == seq + 3);
}

static void test_fade_in(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100);
    lamp_core_set(&c, 0, true, 0, 1000);
    CHECK(c.on && c.level == 0 && lamp_core_busy(&c));

    lamp_core_tick(&c, 500);
    CHECK(c.level >= LAMP_LEVEL_MAX / 2 - 1 && c.level <= LAMP_LEVEL_MAX / 2 + 1);
    lamp_core_tick(&c, 999);
    CHECK(c.level < LAMP_LEVEL_MAX && lamp_core_busy(&c));
    lamp_core_tick(&c, 1000);
    CHECK(c.level == LAMP_LEVEL_MAX && !lamp_core_busy(&c));
}

static void test_fade_out_and_reverse(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 100);
    lamp_core_toggle(&c, 0, 1000);
    CHECK(!c.on && c.level == LAMP_LEVEL_MAX && lamp_core_busy(&c));

    lamp_core_tick(&c, 250);
    uint16_t at_quarter = c.level;
    CHECK(at_quarter < LAMP_LEVEL_MAX && at_quarter > LAMP_LEVEL_MAX / 2);

    /* A tap in the middle of the fade turns it around from where it is. */
    lamp_core_toggle(&c, 250, 1000);
    CHECK(c.on && c.level == at_quarter);
    lamp_core_tick(&c, 750);
    CHECK(c.level > at_quarter && c.level < LAMP_LEVEL_MAX);
    lamp_core_tick(&c, 1250);
    CHECK(c.level == LAMP_LEVEL_MAX && !lamp_core_busy(&c));

    lamp_core_toggle(&c, 2000, 1000);
    lamp_core_tick(&c, 3000);
    CHECK(!c.on && c.level == 0 && !lamp_core_busy(&c));
}

static void test_fade_survives_long_transition(void)
{
    /* A 30-minute sunrise. */
    lamp_core_t c;
    lamp_core_init(&c, false, 80);
    lamp_core_set(&c, 0, true, 0, 30 * 60 * 1000);
    uint16_t prev = 0;
    for (int64_t t = 0; t <= 30 * 60 * 1000; t += 10000) {
        lamp_core_tick(&c, t);
        CHECK(c.level >= prev);
        prev = c.level;
    }
    CHECK(c.level == lamp_pct_to_level(80) && !lamp_core_busy(&c));
}

static void test_hold_ramp(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 50);
    uint16_t start = c.level;

    lamp_core_hold_begin(&c, 0);
    CHECK(lamp_core_busy(&c));
    lamp_core_tick(&c, 300);
    CHECK(c.level > start);
    CHECK(c.brightness == lamp_level_to_pct(c.level));

    lamp_core_tick(&c, LAMP_RAMP_MS);
    CHECK(c.level == LAMP_LEVEL_MAX && c.brightness == 100);
    lamp_core_tick(&c, LAMP_RAMP_MS + 5000);
    CHECK(c.level == LAMP_LEVEL_MAX);

    lamp_core_hold_end(&c, LAMP_RAMP_MS + 5000);
    CHECK(!lamp_core_busy(&c) && c.on && c.brightness == 100);

    /* The next hold goes the other way and stops at the floor, not at off. */
    lamp_core_hold_begin(&c, 10000);
    lamp_core_tick(&c, 10000 + LAMP_RAMP_MS / 2);
    CHECK(c.level < LAMP_LEVEL_MAX && c.level > FLOOR);
    lamp_core_tick(&c, 10000 + 2 * LAMP_RAMP_MS);
    CHECK(c.level == FLOOR && c.brightness == LAMP_PCT_MIN && c.on);
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
    lamp_core_init(&c, true, 50);
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
    lamp_core_init(&c, false, 80);
    uint32_t seq = c.seq;

    lamp_core_hold_begin(&c, 0);
    CHECK(c.on && c.level == FLOOR && c.seq == seq + 1);
    lamp_core_tick(&c, 1500);
    CHECK(c.level > FLOOR && c.level < LAMP_LEVEL_MAX);
    lamp_core_hold_end(&c, 1500);
    CHECK(c.on && c.brightness == lamp_level_to_pct(c.level));
}

static void test_hold_takes_over_fade(void)
{
    lamp_core_t c;
    lamp_core_init(&c, false, 100);
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
    lamp_core_init(&c, true, 50);
    lamp_core_hold_begin(&c, 0);
    lamp_core_set(&c, 100, true, 20, 0);
    lamp_core_tick(&c, 1000);
    CHECK(c.level == lamp_pct_to_level(20) && c.brightness == 20);
    lamp_core_hold_end(&c, 1100);
    CHECK(c.level == lamp_pct_to_level(20));
}

static void test_signal(void)
{
    lamp_core_t c;
    lamp_core_init(&c, true, 30);
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
    test_fade_out_and_reverse();
    test_fade_survives_long_transition();
    test_hold_ramp();
    test_hold_alternates();
    test_hold_from_off();
    test_hold_takes_over_fade();
    test_set_ends_hold();
    test_signal();
    REPORT();
}
