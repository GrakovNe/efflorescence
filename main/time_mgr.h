/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "appconf.h"

void time_mgr_start(const appconf_t *cfg);

/* True once the clock can be trusted: set from NTP, or kept by the RTC
 * across a restart. */
bool time_mgr_synced(void);

/* -1 while the clock is not set. */
int64_t time_mgr_unix(void);

/* Local time in the configured zone; false while the clock is not set. */
bool time_mgr_local(struct tm *out);
