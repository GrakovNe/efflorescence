/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

void web_server_start(const appconf_t *cfg, const char device_id[5]);

bool web_server_up(void);

#ifdef __cplusplus
}
#endif
