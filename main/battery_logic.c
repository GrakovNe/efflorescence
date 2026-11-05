/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "battery_logic.h"

battery_boot_t battery_on_boot(battery_state_t *s, battery_reset_t why)
{
    s->on_for_s = 0;
    switch (why) {
    case BATTERY_RST_POWER_ON:
        s->low = false;
        s->streak = 0;
        return BATTERY_BOOT_NORMAL;
    case BATTERY_RST_BROWNOUT:
        s->low = true;
        if (s->streak < UINT8_MAX) {
            s->streak++;
        }
        if (s->streak >= BATTERY_STREAK_LIMIT) {
            s->streak = 0;
            return BATTERY_BOOT_SLEEP;
        }
        return BATTERY_BOOT_DARK;
    default:
        s->streak = 0;
        return BATTERY_BOOT_NORMAL;
    }
}

bool battery_tick(battery_state_t *s, bool led_on, uint32_t uptime_s,
                  uint32_t dt_s)
{
    if (uptime_s >= BATTERY_SETTLE_S) {
        s->streak = 0;
    }
    if (!s->low) {
        return false;
    }
    s->on_for_s = led_on ? s->on_for_s + dt_s : 0;
    if (s->on_for_s < BATTERY_CLEAR_AFTER_S) {
        return false;
    }
    s->low = false;
    s->on_for_s = 0;
    return true;
}

bool battery_watching(const battery_state_t *s)
{
    return s->low || s->streak > 0;
}
