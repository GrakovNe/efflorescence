/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "config_merge.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool merge_str(const cJSON *obj, const char *key, char *dst, size_t dst_sz,
                      char *err, size_t err_sz)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNull(v)) {
        snprintf(err, err_sz, "\"%s\" must be a string", key);
        return false;
    }
    if (v == NULL) {
        return true;
    }
    if (!cJSON_IsString(v) || strnlen(v->valuestring, dst_sz) >= dst_sz) {
        snprintf(err, err_sz, "\"%s\" must be a string of at most %u bytes",
                 key, (unsigned)dst_sz - 1);
        return false;
    }
    strcpy(dst, v->valuestring);
    return true;
}

static bool merge_bool(const cJSON *obj, const char *key, bool *dst,
                       char *err, size_t err_sz)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == NULL) {
        return true;
    }
    if (!cJSON_IsBool(v)) {
        snprintf(err, err_sz, "\"%s\" must be true or false", key);
        return false;
    }
    *dst = cJSON_IsTrue(v);
    return true;
}

static bool merge_u16(const cJSON *obj, const char *key, uint16_t *dst,
                      int min, int max, char *err, size_t err_sz)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(v) || v->valuedouble < min || v->valuedouble > max ||
        v->valuedouble != floor(v->valuedouble)) {
        snprintf(err, err_sz, "\"%s\" must be an integer between %d and %d",
                 key, min, max);
        return false;
    }
    *dst = (uint16_t)v->valuedouble;
    return true;
}

static bool merge_mqtt(const cJSON *mq, appconf_t *c, char *err, size_t err_sz)
{
    return merge_bool(mq, "enabled", &c->mqtt_enabled, err, err_sz) &&
           merge_str(mq, "host", c->mqtt_host, sizeof c->mqtt_host, err, err_sz) &&
           merge_u16(mq, "port", &c->mqtt_port, 1, 65535, err, err_sz) &&
           merge_str(mq, "username", c->mqtt_username, sizeof c->mqtt_username,
                     err, err_sz) &&
           merge_str(mq, "password", c->mqtt_password, sizeof c->mqtt_password,
                     err, err_sz) &&
           merge_bool(mq, "tls", &c->mqtt_tls, err, err_sz) &&
           merge_str(mq, "ca_cert", c->mqtt_ca_cert, sizeof c->mqtt_ca_cert,
                     err, err_sz) &&
           merge_bool(mq, "skip_verify", &c->mqtt_skip_verify, err, err_sz) &&
           merge_str(mq, "topic_prefix", c->mqtt_prefix, sizeof c->mqtt_prefix,
                     err, err_sz) &&
           merge_bool(mq, "discovery", &c->mqtt_discovery, err, err_sz);
}

static bool section(const cJSON *root, const char *key, const cJSON **out,
                    char *err, size_t err_sz)
{
    *out = cJSON_GetObjectItemCaseSensitive(root, key);
    if (*out != NULL && !cJSON_IsObject(*out)) {
        snprintf(err, err_sz, "\"%s\" must be an object", key);
        return false;
    }
    return true;
}

bool config_merge(const cJSON *root, appconf_t *c, char *err, size_t err_sz)
{
    const cJSON *o;
    bool ok = section(root, "device", &o, err, err_sz) &&
              (o == NULL || merge_str(o, "name", c->device_name,
                                      sizeof c->device_name, err, err_sz));
    ok = ok && section(root, "wifi", &o, err, err_sz) &&
         (o == NULL || (merge_str(o, "ssid", c->wifi_ssid, sizeof c->wifi_ssid,
                                  err, err_sz) &&
                        merge_str(o, "password", c->wifi_password,
                                  sizeof c->wifi_password, err, err_sz)));
    ok = ok && section(root, "time", &o, err, err_sz) &&
         (o == NULL || (merge_str(o, "ntp_server", c->ntp_server,
                                  sizeof c->ntp_server, err, err_sz) &&
                        merge_str(o, "tz", c->tz, sizeof c->tz, err, err_sz)));
    ok = ok && section(root, "light", &o, err, err_sz) &&
         (o == NULL || merge_bool(o, "soft", &c->soft_switch, err, err_sz));
    ok = ok && section(root, "power", &o, err, err_sz) &&
         (o == NULL || merge_bool(o, "battery", &c->battery, err, err_sz));
    ok = ok && section(root, "mqtt", &o, err, err_sz) &&
         (o == NULL || merge_mqtt(o, c, err, err_sz));
    return ok && appconf_validate(c, err, err_sz);
}
