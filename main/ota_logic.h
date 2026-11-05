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

#define OTA_VERSION_MAX 31
#define OTA_URL_MAX     191

/* A fresh image has this long to prove itself before it is rolled back. */
#define OTA_HEALTH_DEADLINE_S 180
/* Without Wi-Fi settings there is nothing to connect to: the setup AP
 * and the web server staying up this long is the proof. */
#define OTA_HEALTH_AP_SETTLE_S 20

/* Release versions look like "v1.0.123"; the "v" is optional. */
bool ota_version_parse(const char *s, int out[3]);

/* True when `candidate` is a release and `current` is an older release
 * or not a release at all (a development build). */
bool ota_version_newer(const char *candidate, const char *current);

typedef struct {
    char version[OTA_VERSION_MAX + 1];
    char url[OTA_URL_MAX + 1];
} ota_manifest_t;

/* {"version": "v1.0.123", "app": "efflorescence-app.bin"}
 * "app" is an http(s) URL or a file name next to the manifest. */
bool ota_manifest_parse(const cJSON *root, const char *manifest_url,
                        ota_manifest_t *out, char *err, size_t err_sz);

typedef enum {
    OTA_HEALTH_WAIT,
    OTA_HEALTH_CONFIRM,
    OTA_HEALTH_ROLLBACK,
} ota_health_t;

typedef struct {
    bool     wifi_configured;
    bool     sta_connected;
    bool     ap_up;
    bool     web_up;
    uint32_t uptime_s;
} ota_health_in_t;

/* The device is sealed: an image counts as good only once the way the
 * next update would arrive is open again. */
ota_health_t ota_health(const ota_health_in_t *in);

#ifdef __cplusplus
}
#endif
