/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ha_discovery.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const struct {
    const char *component;
    const char *suffix;
} ENTITY[HA_ENTITY_COUNT] = {
    [HA_LIGHT]       = { "light", "light" },
    [HA_SOFT_SWITCH] = { "switch", "soft" },
    [HA_TAPS]        = { "event", "taps" },
    [HA_UPDATE]      = { "update", "firmware" },
    [HA_BATTERY]     = { "binary_sensor", "battery" },
};

static const char *const TAP_TYPES[] = { "double", "triple", "quadruple", "many" };

static void unique_id(const ha_dev_t *d, ha_entity_t e, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "efflorescence_%s_%s", d->device_id, ENTITY[e].suffix);
}

void ha_config_topic(const ha_dev_t *d, ha_entity_t e, char *out, size_t out_sz)
{
    char id[40];
    unique_id(d, e, id, sizeof id);
    snprintf(out, out_sz, "homeassistant/%s/%s/config", ENTITY[e].component, id);
}

static void add_topic(cJSON *root, const char *key, const ha_dev_t *d,
                      const char *leaf)
{
    char topic[HA_TOPIC_MAX];
    snprintf(topic, sizeof topic, "%s/%s", d->prefix, leaf);
    cJSON_AddStringToObject(root, key, topic);
}

char *ha_config_json(const ha_dev_t *d, ha_entity_t e)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    if (root == NULL || dev == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    char id[40];
    unique_id(d, e, id, sizeof id);
    cJSON_AddStringToObject(root, "unique_id", id);
    add_topic(root, "availability_topic", d, "availability");

    switch (e) {
    case HA_LIGHT:
        /* No name of its own: the entity is the device. */
        cJSON_AddNullToObject(root, "name");
        cJSON_AddStringToObject(root, "schema", "json");
        add_topic(root, "command_topic", d, "set");
        add_topic(root, "state_topic", d, "state");
        cJSON_AddBoolToObject(root, "brightness", true);
        cJSON_AddNumberToObject(root, "brightness_scale", 100);
        cJSON *modes = cJSON_AddArrayToObject(root, "supported_color_modes");
        cJSON_AddItemToArray(modes, cJSON_CreateString("brightness"));
        break;
    case HA_SOFT_SWITCH:
        cJSON_AddStringToObject(root, "name", "Soft on and off");
        add_topic(root, "command_topic", d, "set");
        cJSON_AddStringToObject(root, "payload_on", "{\"soft\":true}");
        cJSON_AddStringToObject(root, "payload_off", "{\"soft\":false}");
        add_topic(root, "state_topic", d, "state");
        cJSON_AddStringToObject(root, "value_template",
                                "{{ 'ON' if value_json.soft else 'OFF' }}");
        cJSON_AddStringToObject(root, "state_on", "ON");
        cJSON_AddStringToObject(root, "state_off", "OFF");
        cJSON_AddStringToObject(root, "entity_category", "config");
        break;
    case HA_TAPS: {
        cJSON_AddStringToObject(root, "name", "Taps");
        add_topic(root, "state_topic", d, "event");
        cJSON *types = cJSON_AddArrayToObject(root, "event_types");
        for (size_t i = 0; i < sizeof TAP_TYPES / sizeof TAP_TYPES[0]; i++) {
            cJSON_AddItemToArray(types, cJSON_CreateString(TAP_TYPES[i]));
        }
        break;
    }
    case HA_UPDATE:
        cJSON_AddStringToObject(root, "name", "Firmware");
        cJSON_AddStringToObject(root, "device_class", "firmware");
        add_topic(root, "state_topic", d, "update");
        add_topic(root, "command_topic", d, "update/install");
        cJSON_AddStringToObject(root, "payload_install", "install");
        break;
    case HA_BATTERY:
        cJSON_AddStringToObject(root, "name", "Battery");
        cJSON_AddStringToObject(root, "device_class", "battery");
        add_topic(root, "state_topic", d, "state");
        cJSON_AddStringToObject(root, "value_template",
                                "{{ 'ON' if value_json.battery_low else 'OFF' }}");
        cJSON_AddStringToObject(root, "entity_category", "diagnostic");
        break;
    default:
        break;
    }

    char dev_id[24];
    char url[96];
    snprintf(dev_id, sizeof dev_id, "efflorescence_%s", d->device_id);
    snprintf(url, sizeof url, "http://%s.local/", d->hostname);
    cJSON_AddStringToObject(dev, "identifiers", dev_id);
    cJSON_AddStringToObject(dev, "name", d->name);
    cJSON_AddStringToObject(dev, "manufacturer", "GrakovNe");
    cJSON_AddStringToObject(dev, "model", "Efflorescence");
    cJSON_AddStringToObject(dev, "sw_version", d->version);
    cJSON_AddStringToObject(dev, "configuration_url", url);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *ha_taps_json(uint8_t taps)
{
    if (taps < 2) {
        return NULL;
    }
    size_t last = sizeof TAP_TYPES / sizeof TAP_TYPES[0] - 1;
    size_t i = (size_t)taps - 2 < last ? (size_t)taps - 2 : last;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "event_type", TAP_TYPES[i]);
    cJSON_AddNumberToObject(root, "count", taps);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

char *ha_update_json(const char *installed, const char *latest,
                     bool in_progress, int progress)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "installed_version", installed);
    cJSON_AddStringToObject(root, "latest_version",
                            latest != NULL && latest[0] != '\0' ? latest
                                                                : installed);
    cJSON_AddBoolToObject(root, "in_progress", in_progress);
    if (progress >= 0) {
        cJSON_AddNumberToObject(root, "update_percentage", progress);
    } else {
        cJSON_AddNullToObject(root, "update_percentage");
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
