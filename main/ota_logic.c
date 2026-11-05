/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ota_logic.h"

#include <stdio.h>
#include <string.h>

bool ota_version_parse(const char *s, int out[3])
{
    if (*s == 'v') {
        s++;
    }
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        int v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++digits > 7) {
                return false;
            }
            v = v * 10 + (*s - '0');
            s++;
        }
        out[i] = v;
        if (i < 2) {
            if (*s != '.') {
                return false;
            }
            s++;
        }
    }
    return *s == '\0';
}

bool ota_version_newer(const char *candidate, const char *current)
{
    int a[3], b[3];
    if (!ota_version_parse(candidate, a)) {
        return false;
    }
    if (!ota_version_parse(current, b)) {
        return true;
    }
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return false;
}

static bool is_url(const char *s)
{
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

static bool file_name_ok(const char *s)
{
    if (s[0] == '\0' || s[0] == '/' || strstr(s, "..") != NULL) {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                  c == '_' || c == '/';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool ota_manifest_parse(const cJSON *root, const char *manifest_url,
                        ota_manifest_t *out, char *err, size_t err_sz)
{
    memset(out, 0, sizeof *out);
    if (!cJSON_IsObject(root)) {
        snprintf(err, err_sz, "the manifest is not a JSON object");
        return false;
    }
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *app = cJSON_GetObjectItemCaseSensitive(root, "app");
    int parts[3];
    if (!cJSON_IsString(ver) || strlen(ver->valuestring) > OTA_VERSION_MAX ||
        !ota_version_parse(ver->valuestring, parts)) {
        snprintf(err, err_sz, "the manifest has no release version");
        return false;
    }
    if (!cJSON_IsString(app)) {
        snprintf(err, err_sz, "the manifest names no image");
        return false;
    }

    int n;
    if (is_url(app->valuestring)) {
        n = snprintf(out->url, sizeof out->url, "%s", app->valuestring);
    } else {
        const char *slash = strrchr(manifest_url, '/');
        if (!is_url(manifest_url) || slash == NULL ||
            !file_name_ok(app->valuestring)) {
            snprintf(err, err_sz, "the manifest names a bad image path");
            return false;
        }
        n = snprintf(out->url, sizeof out->url, "%.*s%s",
                     (int)(slash - manifest_url + 1), manifest_url,
                     app->valuestring);
    }
    if (n < 0 || (size_t)n >= sizeof out->url) {
        out->url[0] = '\0';
        snprintf(err, err_sz, "the image URL is too long");
        return false;
    }
    strcpy(out->version, ver->valuestring);
    return true;
}

ota_health_t ota_health(const ota_health_in_t *in)
{
    bool reachable;
    if (in->wifi_configured) {
        reachable = in->sta_connected;
    } else {
        reachable = in->ap_up && in->uptime_s >= OTA_HEALTH_AP_SETTLE_S;
    }
    if (in->web_up && reachable) {
        return OTA_HEALTH_CONFIRM;
    }
    return in->uptime_s >= OTA_HEALTH_DEADLINE_S ? OTA_HEALTH_ROLLBACK
                                                 : OTA_HEALTH_WAIT;
}
