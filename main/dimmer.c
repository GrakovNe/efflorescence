/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "dimmer.h"

/* L* = 100 * level / DIMMER_LEVEL_MAX.
 * Y = L* / 903.3 for L* <= 8, ((L* + 16) / 116)^3 above. */
#define LINEAR_TOP   5243u                       /* L* = 8 */
#define LINEAR_DEN   591977u                     /* 903.3 * 65535 / 100 */
#define CUBE_OFFSET  (16u * DIMMER_LEVEL_MAX)
#define CUBE_DEN     (116u * DIMMER_LEVEL_MAX)

uint32_t dimmer_duty(uint16_t level, uint32_t duty_max)
{
    uint64_t span = duty_max;
    uint64_t duty;
    if (level <= LINEAR_TOP) {
        duty = (span * level + LINEAR_DEN / 2) / LINEAR_DEN;
    } else {
        uint64_t r = 100ull * level + CUBE_OFFSET;
        uint64_t r2 = r * r / CUBE_DEN;
        uint64_t r3 = r2 * r / CUBE_DEN;
        duty = (span * r3 + CUBE_DEN / 2) / CUBE_DEN;
    }
    return duty > span ? (uint32_t)span : (uint32_t)duty;
}

uint16_t dimmer_level_for_duty(uint32_t duty, uint32_t duty_max)
{
    uint32_t lo = 0;
    uint32_t hi = DIMMER_LEVEL_MAX;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (dimmer_duty((uint16_t)mid, duty_max) >= duty) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return (uint16_t)lo;
}
