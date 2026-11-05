/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HA_TOPIC_MAX 96

typedef struct {
    const char *prefix;    /* the MQTT topic prefix */
    const char *device_id; /* "c6e0" */
    const char *name;
    const char *hostname;
    const char *version;
} ha_dev_t;

typedef enum {
    HA_LIGHT,
    HA_SOFT_SWITCH,
    HA_TAPS,
    HA_UPDATE,
    HA_BATTERY,
    HA_ENTITY_COUNT,
} ha_entity_t;

/* "homeassistant/<component>/<unique id>/config". The unique id is bound
 * to the device id, never to the name, so a rename keeps the entities. */
void ha_config_topic(const ha_dev_t *d, ha_entity_t e, char *out, size_t out_sz);

/* The config payload; free() it. NULL when out of memory. */
char *ha_config_json(const ha_dev_t *d, ha_entity_t e);

/* What goes to "<prefix>/event" after a series of taps:
 * {"event_type": "double", "count": 2}. NULL for fewer than two. */
char *ha_taps_json(uint8_t taps);

/* What goes to "<prefix>/update". latest NULL or empty means not known,
 * and is reported as the installed version. progress < 0 means none. */
char *ha_update_json(const char *installed, const char *latest,
                     bool in_progress, int progress);

#ifdef __cplusplus
}
#endif
