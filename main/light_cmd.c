/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "light_cmd.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool light_cmd_parse(const cJSON *root, light_cmd_t *cmd, char *err,
                     size_t err_sz)
{
    cmd->state = LIGHT_KEEP;
    cmd->brightness = 0;
    cmd->transition_ms = -1;
    cmd->soft = -1;

    if (!cJSON_IsObject(root)) {
        snprintf(err, err_sz, "the command must be a JSON object");
        return false;
    }

    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (v != NULL) {
        if (!cJSON_IsString(v)) {
            snprintf(err, err_sz, "\"state\" must be \"ON\", \"OFF\" or \"TOGGLE\"");
            return false;
        }
        if (strcmp(v->valuestring, "ON") == 0) {
            cmd->state = LIGHT_ON;
        } else if (strcmp(v->valuestring, "OFF") == 0) {
            cmd->state = LIGHT_OFF;
        } else if (strcmp(v->valuestring, "TOGGLE") == 0) {
            cmd->state = LIGHT_TOGGLE;
        } else {
            snprintf(err, err_sz, "\"state\" must be \"ON\", \"OFF\" or \"TOGGLE\"");
            return false;
        }
    }

    v = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (v != NULL) {
        if (!cJSON_IsNumber(v) || v->valuedouble < 1 || v->valuedouble > 100 ||
            v->valuedouble != floor(v->valuedouble)) {
            snprintf(err, err_sz, "\"brightness\" must be an integer between 1 and 100");
            return false;
        }
        cmd->brightness = (uint8_t)v->valuedouble;
    }

    v = cJSON_GetObjectItemCaseSensitive(root, "transition");
    if (v != NULL) {
        if (!cJSON_IsNumber(v) || v->valuedouble < 0 ||
            v->valuedouble > LIGHT_TRANSITION_MAX_S) {
            snprintf(err, err_sz, "\"transition\" must be between 0 and %d seconds",
                     LIGHT_TRANSITION_MAX_S);
            return false;
        }
        cmd->transition_ms = (int32_t)lround(v->valuedouble * 1000.0);
    }

    v = cJSON_GetObjectItemCaseSensitive(root, "soft");
    if (v != NULL) {
        if (!cJSON_IsBool(v)) {
            snprintf(err, err_sz, "\"soft\" must be true or false");
            return false;
        }
        cmd->soft = cJSON_IsTrue(v) ? 1 : 0;
    }
    return true;
}

bool light_cmd_parse_text(const char *data, size_t len, light_cmd_t *cmd,
                          char *err, size_t err_sz)
{
    static const struct {
        const char *word;
        light_state_t state;
    } words[] = {
        { "ON", LIGHT_ON }, { "OFF", LIGHT_OFF }, { "TOGGLE", LIGHT_TOGGLE },
    };
    while (len > 0 && (data[len - 1] == '\n' || data[len - 1] == '\r' ||
                       data[len - 1] == ' ')) {
        len--;
    }
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        if (len == strlen(words[i].word) &&
            memcmp(data, words[i].word, len) == 0) {
            cmd->state = words[i].state;
            cmd->brightness = 0;
            cmd->transition_ms = -1;
            cmd->soft = -1;
            return true;
        }
    }
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        snprintf(err, err_sz, "the command is neither JSON nor ON, OFF, TOGGLE");
        return false;
    }
    bool ok = light_cmd_parse(root, cmd, err, err_sz);
    cJSON_Delete(root);
    return ok;
}

cJSON *light_state_json(bool on, uint8_t brightness, bool soft,
                        bool battery_low)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    if (cJSON_AddStringToObject(o, "state", on ? "ON" : "OFF") == NULL ||
        cJSON_AddNumberToObject(o, "brightness", brightness) == NULL ||
        cJSON_AddBoolToObject(o, "soft", soft) == NULL ||
        cJSON_AddBoolToObject(o, "battery_low", battery_low) == NULL) {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}
