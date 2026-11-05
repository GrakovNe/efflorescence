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

uint32_t dimmer_duty(uint16_t level, uint32_t duty_min, uint32_t duty_max)
{
    if (level == 0) {
        return 0;
    }
    if (duty_max <= duty_min) {
        return duty_max;
    }
    uint64_t span = duty_max - duty_min;
    uint64_t part;
    if (level <= LINEAR_TOP) {
        part = (span * level + LINEAR_DEN / 2) / LINEAR_DEN;
    } else {
        uint64_t r = 100ull * level + CUBE_OFFSET;
        uint64_t r2 = r * r / CUBE_DEN;
        uint64_t r3 = r2 * r / CUBE_DEN;
        part = (span * r3 + CUBE_DEN / 2) / CUBE_DEN;
    }
    if (part > span) {
        part = span;
    }
    return duty_min + (uint32_t)part;
}
