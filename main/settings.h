/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "appconf.h"
#include "schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

void settings_nvs_init(void);

void settings_load(appconf_t *c, const char device_id[5]);

esp_err_t settings_save(const appconf_t *c);

/* The one setting that changes without a restart. */
esp_err_t settings_save_soft(bool soft);

/* Leaves *on and *brightness alone when nothing was stored. */
void settings_load_state(bool *on, uint8_t *brightness);

esp_err_t settings_save_state(bool on, uint8_t brightness);

/* Empty when nothing usable was stored. */
void settings_load_schedule(sched_t *out);

esp_err_t settings_save_schedule(const sched_t *s);

#ifdef __cplusplus
}
#endif
