/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void settings_nvs_init(void);

/* Leaves *on and *brightness alone when nothing was stored. */
void settings_load_state(bool *on, uint8_t *brightness);

esp_err_t settings_save_state(bool on, uint8_t brightness);

#ifdef __cplusplus
}
#endif
