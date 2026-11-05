/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_mgr_start(const appconf_t *cfg, const char device_id[5]);

typedef struct {
    bool enabled;
    bool connected;
    int64_t last_publish;
    uint32_t errors;
} mqtt_mgr_status_t;

void mqtt_mgr_get_status(mqtt_mgr_status_t *out);

/* A finished series of taps on the sensor; fewer than two is not news. */
void mqtt_mgr_taps(uint8_t taps);

#ifdef __cplusplus
}
#endif
