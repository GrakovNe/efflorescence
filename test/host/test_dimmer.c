/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "dimmer.h"

#include "tinytest.h"

#define DUTY_MAX 16383u

static void test_ends(void)
{
    CHECK(dimmer_duty(0, 0, DUTY_MAX) == 0);
    CHECK(dimmer_duty(0, 160, DUTY_MAX) == 0);
    CHECK(dimmer_duty(1, 160, DUTY_MAX) == 160);
    CHECK(dimmer_duty(DIMMER_LEVEL_MAX, 0, DUTY_MAX) == DUTY_MAX);
    CHECK(dimmer_duty(DIMMER_LEVEL_MAX, 160, DUTY_MAX) == DUTY_MAX);
}

/* Y for L* = 50 is 0.1842, for L* = 25 it is 0.0442. */
static void test_curve(void)
{
    uint32_t half = dimmer_duty(DIMMER_LEVEL_MAX / 2, 0, DUTY_MAX);
    CHECK(half >= 3015 && half <= 3020);
    uint32_t quarter = dimmer_duty(DIMMER_LEVEL_MAX / 4, 0, DUTY_MAX);
    CHECK(quarter >= 722 && quarter <= 726);
}

static void test_monotonic(void)
{
    uint32_t prev = 0;
    uint32_t max_step = 0;
    for (uint32_t level = 1; level <= DIMMER_LEVEL_MAX; level++) {
        uint32_t duty = dimmer_duty((uint16_t)level, 0, DUTY_MAX);
        if (duty < prev) {
            CHECK(duty >= prev);
            return;
        }
        if (duty - prev > max_step) {
            max_step = duty - prev;
        }
        prev = duty;
    }
    CHECK(prev == DUTY_MAX);
    /* No jump at the joint between the linear and the cubic part. */
    CHECK(max_step <= 1);
}

static void test_degenerate_range(void)
{
    CHECK(dimmer_duty(1000, 500, 500) == 500);
    CHECK(dimmer_duty(1000, 600, 500) == 500);
    CHECK(dimmer_duty(0, 600, 500) == 0);
}

int main(void)
{
    test_ends();
    test_curve();
    test_monotonic();
    test_degenerate_range();
    REPORT();
}
