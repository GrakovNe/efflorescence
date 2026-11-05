/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#include "light_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_MAX          12
#define SCHED_DAYS_ALL     0x7f
#define SCHED_MINUTES      1440
#define SCHED_WEEK_MINUTES (7 * SCHED_MINUTES)

typedef struct {
    bool     enabled;
    uint8_t  days;         /* bit 0 = Monday ... bit 6 = Sunday */
    uint16_t minute;       /* of the day, local time: when the action starts */
    bool     on;
    uint8_t  brightness;   /* percent for "on", 0 = as it was */
    uint16_t transition_s; /* 0 = the lamp's own soft on/off */
} sched_entry_t;

typedef struct {
    uint8_t count;
    sched_entry_t entry[SCHED_MAX];
} sched_t;

bool sched_validate(const sched_t *s, char *err, size_t err_sz);

/* The enabled entries that start at this weekday (0 = Monday) and
 * minute, in list order. Returns how many. */
int sched_due(const sched_t *s, int wday, int minute, uint8_t out[SCHED_MAX]);

/* The enabled entry that starts soonest after this moment, -1 if none.
 * An entry due right now counts as a week away. */
int sched_next(const sched_t *s, int wday, int minute, int *in_minutes);

/* What the lamp is told when an entry starts. */
void sched_command(const sched_entry_t *e, light_cmd_t *cmd);

/* {"entries": [{"enabled": true, "days": [1, 2, 3, 4, 5], "time": "07:00",
 *               "state": "ON", "brightness": 60, "transition": 1800}]}
 * days are ISO numbers, 1 = Monday; brightness and transition are
 * optional. */
bool sched_from_json(const cJSON *root, sched_t *out, char *err, size_t err_sz);

/* NULL when out of memory. */
cJSON *sched_to_json(const sched_t *s);

#ifdef __cplusplus
}
#endif
