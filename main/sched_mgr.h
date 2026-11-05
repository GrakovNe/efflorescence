/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool clock_set;
    int  wday;       /* 0 = Monday */
    int  minute;     /* of the day */
    int  next;       /* index of the next entry to start, -1 if none */
    int  next_in_min;
} sched_mgr_status_t;

void sched_mgr_start(void);

void sched_mgr_get(sched_t *out);

/* Stores the list and makes it the one that runs. */
esp_err_t sched_mgr_set(const sched_t *s);

void sched_mgr_get_status(sched_mgr_status_t *out);

#ifdef __cplusplus
}
#endif
