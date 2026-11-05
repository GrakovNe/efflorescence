/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "battery_logic.h"

#include "tinytest.h"

static void test_fresh_power(void)
{
    battery_state_t s = { .low = true, .streak = 2, .on_for_s = 99 };
    CHECK(battery_on_boot(&s, BATTERY_RST_POWER_ON) == BATTERY_BOOT_NORMAL);
    CHECK(!s.low && s.streak == 0 && s.on_for_s == 0);
    CHECK(!battery_watching(&s));
}

static void test_one_brownout(void)
{
    battery_state_t s = { 0 };
    CHECK(battery_on_boot(&s, BATTERY_RST_BROWNOUT) == BATTERY_BOOT_DARK);
    CHECK(s.low && s.streak == 1);
    CHECK(battery_watching(&s));

    /* A restart for any other reason keeps the flag and ends the row. */
    CHECK(battery_on_boot(&s, BATTERY_RST_OTHER) == BATTERY_BOOT_NORMAL);
    CHECK(s.low && s.streak == 0);
}

static void test_row_ends_in_sleep(void)
{
    battery_state_t s = { 0 };
    CHECK(battery_on_boot(&s, BATTERY_RST_BROWNOUT) == BATTERY_BOOT_DARK);
    CHECK(battery_on_boot(&s, BATTERY_RST_BROWNOUT) == BATTERY_BOOT_DARK);
    CHECK(battery_on_boot(&s, BATTERY_RST_BROWNOUT) == BATTERY_BOOT_SLEEP);
    CHECK(s.low && s.streak == 0);

    /* The touch that wakes it starts over; the flag is still up. */
    CHECK(battery_on_boot(&s, BATTERY_RST_OTHER) == BATTERY_BOOT_NORMAL);
    CHECK(s.low);
    CHECK(battery_on_boot(&s, BATTERY_RST_BROWNOUT) == BATTERY_BOOT_DARK);
}

static void test_row_broken_by_uptime(void)
{
    battery_state_t s = { 0 };
    battery_on_boot(&s, BATTERY_RST_BROWNOUT);
    battery_on_boot(&s, BATTERY_RST_BROWNOUT);
    CHECK(s.streak == 2);
    CHECK(!battery_tick(&s, false, BATTERY_SETTLE_S - 1, 5));
    CHECK(s.streak == 2);
    CHECK(!battery_tick(&s, false, BATTERY_SETTLE_S, 5));
    CHECK(s.streak == 0 && s.low);
    CHECK(battery_on_boot(&s, BATTERY_RST_BROWNOUT) == BATTERY_BOOT_DARK);
}

static void test_flag_clears_after_light(void)
{
    battery_state_t s = { 0 };
    battery_on_boot(&s, BATTERY_RST_BROWNOUT);
    uint32_t up = 0;

    /* Dark does not count, and a break starts the count again. */
    for (int i = 0; i < 100; i++) {
        up += 5;
        CHECK(!battery_tick(&s, false, up, 5));
    }
    for (int i = 0; i < BATTERY_CLEAR_AFTER_S / 5 - 1; i++) {
        up += 5;
        CHECK(!battery_tick(&s, true, up, 5));
    }
    up += 5;
    CHECK(!battery_tick(&s, false, up, 5));
    CHECK(s.low && s.on_for_s == 0);

    for (int i = 0; i < BATTERY_CLEAR_AFTER_S / 5 - 1; i++) {
        up += 5;
        CHECK(!battery_tick(&s, true, up, 5));
    }
    up += 5;
    CHECK(battery_tick(&s, true, up, 5));
    CHECK(!s.low && !battery_watching(&s));
    CHECK(!battery_tick(&s, true, up + 5, 5));
}

int main(void)
{
    test_fresh_power();
    test_one_brownout();
    test_row_ends_in_sleep();
    test_row_broken_by_uptime();
    test_flag_clears_after_light();
    REPORT();
}
