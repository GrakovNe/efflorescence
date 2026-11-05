/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "schedule.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool sched_validate(const sched_t *s, char *err, size_t err_sz)
{
    if (s->count > SCHED_MAX) {
        snprintf(err, err_sz, "at most %d entries", SCHED_MAX);
        return false;
    }
    for (int i = 0; i < s->count; i++) {
        const sched_entry_t *e = &s->entry[i];
        if (e->days == 0 || (e->days & ~SCHED_DAYS_ALL) != 0) {
            snprintf(err, err_sz, "entry %d: pick at least one day", i + 1);
            return false;
        }
        if (e->minute >= SCHED_MINUTES) {
            snprintf(err, err_sz, "entry %d: bad time", i + 1);
            return false;
        }
        if (e->brightness > 100 || (!e->on && e->brightness != 0)) {
            snprintf(err, err_sz, "entry %d: bad brightness", i + 1);
            return false;
        }
        if (e->transition_s > LIGHT_TRANSITION_MAX_S) {
            snprintf(err, err_sz, "entry %d: transition is at most %d s", i + 1,
                     LIGHT_TRANSITION_MAX_S);
            return false;
        }
    }
    return true;
}

int sched_due(const sched_t *s, int wday, int minute, uint8_t out[SCHED_MAX])
{
    int n = 0;
    for (int i = 0; i < s->count && i < SCHED_MAX; i++) {
        const sched_entry_t *e = &s->entry[i];
        if (e->enabled && (e->days & (1u << wday)) != 0 && e->minute == minute) {
            out[n++] = (uint8_t)i;
        }
    }
    return n;
}

int sched_next(const sched_t *s, int wday, int minute, int *in_minutes)
{
    int best = -1;
    int best_delta = 0;
    for (int i = 0; i < s->count && i < SCHED_MAX; i++) {
        const sched_entry_t *e = &s->entry[i];
        if (!e->enabled) {
            continue;
        }
        for (int d = 0; d <= 7; d++) {
            if ((e->days & (1u << ((wday + d) % 7))) == 0) {
                continue;
            }
            int delta = d * SCHED_MINUTES + (int)e->minute - minute;
            if (delta <= 0) {
                continue;
            }
            if (best < 0 || delta < best_delta) {
                best = i;
                best_delta = delta;
            }
            break;
        }
    }
    if (best >= 0 && in_minutes != NULL) {
        *in_minutes = best_delta;
    }
    return best;
}

void sched_command(const sched_entry_t *e, light_cmd_t *cmd)
{
    cmd->state = e->on ? LIGHT_ON : LIGHT_OFF;
    cmd->brightness = e->on ? e->brightness : 0;
    cmd->transition_ms = e->transition_s > 0 ? (int32_t)e->transition_s * 1000 : -1;
    cmd->soft = -1;
}

static bool parse_time(const char *s, uint16_t *minute)
{
    if (strlen(s) != 5 || s[2] != ':') {
        return false;
    }
    for (int i = 0; i < 5; i++) {
        if (i != 2 && (s[i] < '0' || s[i] > '9')) {
            return false;
        }
    }
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) {
        return false;
    }
    *minute = (uint16_t)(h * 60 + m);
    return true;
}

static bool is_int_in(const cJSON *v, int min, int max)
{
    return cJSON_IsNumber(v) && v->valuedouble >= min && v->valuedouble <= max &&
           v->valuedouble == floor(v->valuedouble);
}

static bool entry_from_json(const cJSON *o, int n, sched_entry_t *e, char *err,
                            size_t err_sz)
{
    memset(e, 0, sizeof *e);
    if (!cJSON_IsObject(o)) {
        snprintf(err, err_sz, "entry %d must be an object", n);
        return false;
    }

    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "enabled");
    if (v != NULL && !cJSON_IsBool(v)) {
        snprintf(err, err_sz, "entry %d: \"enabled\" must be true or false", n);
        return false;
    }
    e->enabled = v == NULL || cJSON_IsTrue(v);

    v = cJSON_GetObjectItemCaseSensitive(o, "days");
    if (!cJSON_IsArray(v)) {
        snprintf(err, err_sz, "entry %d: \"days\" must be a list of 1..7", n);
        return false;
    }
    const cJSON *d;
    cJSON_ArrayForEach(d, v) {
        if (!is_int_in(d, 1, 7)) {
            snprintf(err, err_sz, "entry %d: \"days\" must be a list of 1..7", n);
            return false;
        }
        e->days |= (uint8_t)(1u << ((int)d->valuedouble - 1));
    }

    v = cJSON_GetObjectItemCaseSensitive(o, "time");
    if (!cJSON_IsString(v) || !parse_time(v->valuestring, &e->minute)) {
        snprintf(err, err_sz, "entry %d: \"time\" must look like \"07:30\"", n);
        return false;
    }

    v = cJSON_GetObjectItemCaseSensitive(o, "state");
    if (cJSON_IsString(v) && strcmp(v->valuestring, "ON") == 0) {
        e->on = true;
    } else if (cJSON_IsString(v) && strcmp(v->valuestring, "OFF") == 0) {
        e->on = false;
    } else {
        snprintf(err, err_sz, "entry %d: \"state\" must be \"ON\" or \"OFF\"", n);
        return false;
    }

    v = cJSON_GetObjectItemCaseSensitive(o, "brightness");
    if (v != NULL && !cJSON_IsNull(v)) {
        if (!e->on || !is_int_in(v, 1, 100)) {
            snprintf(err, err_sz, "entry %d: \"brightness\" is 1..100 and only "
                                  "for \"ON\"", n);
            return false;
        }
        e->brightness = (uint8_t)v->valuedouble;
    }

    v = cJSON_GetObjectItemCaseSensitive(o, "transition");
    if (v != NULL && !cJSON_IsNull(v)) {
        if (!is_int_in(v, 0, LIGHT_TRANSITION_MAX_S)) {
            snprintf(err, err_sz, "entry %d: \"transition\" is 0..%d seconds", n,
                     LIGHT_TRANSITION_MAX_S);
            return false;
        }
        e->transition_s = (uint16_t)v->valuedouble;
    }
    return true;
}

bool sched_from_json(const cJSON *root, sched_t *out, char *err, size_t err_sz)
{
    memset(out, 0, sizeof *out);
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (!cJSON_IsArray(arr)) {
        snprintf(err, err_sz, "\"entries\" must be a list");
        return false;
    }
    int n = cJSON_GetArraySize(arr);
    if (n > SCHED_MAX) {
        snprintf(err, err_sz, "at most %d entries", SCHED_MAX);
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (!entry_from_json(cJSON_GetArrayItem(arr, i), i + 1, &out->entry[i],
                             err, err_sz)) {
            return false;
        }
    }
    out->count = (uint8_t)n;
    return sched_validate(out, err, err_sz);
}

cJSON *sched_to_json(const sched_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "entries");
    if (root == NULL || arr == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    for (int i = 0; i < s->count && i < SCHED_MAX; i++) {
        const sched_entry_t *e = &s->entry[i];
        cJSON *o = cJSON_CreateObject();
        cJSON *days = cJSON_CreateArray();
        if (o == NULL || days == NULL) {
            cJSON_Delete(o);
            cJSON_Delete(days);
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddItemToArray(arr, o);
        cJSON_AddBoolToObject(o, "enabled", e->enabled);
        cJSON_AddItemToObject(o, "days", days);
        for (int d = 0; d < 7; d++) {
            if ((e->days & (1u << d)) != 0) {
                cJSON_AddItemToArray(days, cJSON_CreateNumber(d + 1));
            }
        }
        char time[6];
        snprintf(time, sizeof time, "%02u:%02u", (unsigned)(e->minute / 60) % 24u,
                 (unsigned)(e->minute % 60));
        cJSON_AddStringToObject(o, "time", time);
        cJSON_AddStringToObject(o, "state", e->on ? "ON" : "OFF");
        if (e->on && e->brightness != 0) {
            cJSON_AddNumberToObject(o, "brightness", e->brightness);
        } else {
            cJSON_AddNullToObject(o, "brightness");
        }
        cJSON_AddNumberToObject(o, "transition", e->transition_s);
    }
    return root;
}
