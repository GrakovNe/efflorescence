/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_TRANSITION_MAX_S 7200

typedef enum {
    LIGHT_KEEP,
    LIGHT_ON,
    LIGHT_OFF,
    LIGHT_TOGGLE,
} light_state_t;

typedef struct {
    light_state_t state;
    uint8_t brightness;    /* percent, 0 = keep */
    int32_t transition_ms; /* -1 = the lamp's own soft on/off */
    int8_t  soft;          /* -1 = keep, else the new soft on/off setting */
} light_cmd_t;

/* The one command format for the HTTP API and MQTT, a superset of the
 * Home Assistant JSON light schema:
 *   {"state": "ON" | "OFF" | "TOGGLE", "brightness": 1..100,
 *    "transition": seconds, "soft": true | false}
 * Every field is optional. */
bool light_cmd_parse(const cJSON *root, light_cmd_t *cmd, char *err,
                     size_t err_sz);

/* A raw MQTT payload: the JSON above, or just ON, OFF or TOGGLE. */
bool light_cmd_parse_text(const char *data, size_t len, light_cmd_t *cmd,
                          char *err, size_t err_sz);

/* The state as it goes out over HTTP and MQTT:
 *   {"state": "ON" | "OFF", "brightness": 1..100, "soft": true | false}
 * NULL when out of memory. */
cJSON *light_state_json(bool on, uint8_t brightness, bool soft);

#ifdef __cplusplus
}
#endif
