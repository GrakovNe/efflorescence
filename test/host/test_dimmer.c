/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "dimmer.h"

#include "tinytest.h"

#define DUTY_MAX 16383u

static void test_ends(void)
{
    CHECK(dimmer_duty(0, DUTY_MAX) == 0);
    CHECK(dimmer_duty(DIMMER_LEVEL_MAX, DUTY_MAX) == DUTY_MAX);
    CHECK(dimmer_duty(DIMMER_LEVEL_MAX, 255) == 255);
    CHECK(dimmer_duty(1000, 0) == 0);
}

/* Y for L* = 50 is 0.1842, for L* = 25 it is 0.0442, for L* = 15 0.0191. */
static void test_curve(void)
{
    uint32_t half = dimmer_duty(DIMMER_LEVEL_MAX / 2, DUTY_MAX);
    CHECK(half >= 3015 && half <= 3020);
    uint32_t quarter = dimmer_duty(DIMMER_LEVEL_MAX / 4, DUTY_MAX);
    CHECK(quarter >= 722 && quarter <= 726);
    uint32_t dim = dimmer_duty((uint16_t)(DIMMER_LEVEL_MAX * 15 / 100), DUTY_MAX);
    CHECK(dim >= 311 && dim <= 315);
}

static void test_monotonic(void)
{
    uint32_t prev = 0;
    uint32_t max_step = 0;
    for (uint32_t level = 1; level <= DIMMER_LEVEL_MAX; level++) {
        uint32_t duty = dimmer_duty((uint16_t)level, DUTY_MAX);
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

static void test_level_for_duty(void)
{
    CHECK(dimmer_level_for_duty(0, DUTY_MAX) == 0);
    CHECK(dimmer_level_for_duty(DUTY_MAX, DUTY_MAX) == DIMMER_LEVEL_MAX);
    CHECK(dimmer_level_for_duty(DUTY_MAX + 5, DUTY_MAX) == DIMMER_LEVEL_MAX);

    /* 2.9 % duty sits at about L* = 19.6. */
    uint32_t duty = DUTY_MAX * 29 / 1000;
    uint16_t level = dimmer_level_for_duty(duty, DUTY_MAX);
    CHECK(level > DIMMER_LEVEL_MAX * 19 / 100 && level < DIMMER_LEVEL_MAX * 20 / 100);
    CHECK(dimmer_duty(level, DUTY_MAX) >= duty);
    CHECK(dimmer_duty((uint16_t)(level - 1), DUTY_MAX) < duty);

    for (uint32_t d = 1; d <= DUTY_MAX; d += 37) {
        uint16_t l = dimmer_level_for_duty(d, DUTY_MAX);
        CHECK(dimmer_duty(l, DUTY_MAX) >= d);
        CHECK(l == 0 || dimmer_duty((uint16_t)(l - 1), DUTY_MAX) < d);
    }
}

int main(void)
{
    test_ends();
    test_curve();
    test_monotonic();
    test_level_for_duty();
    REPORT();
}
