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

#define APPCONF_NAME_MAX 32
#define APPCONF_SSID_MAX 32
#define APPCONF_PASS_MAX 64
#define APPCONF_HOST_MAX 64
#define APPCONF_TZ_MAX   47

#define APPCONF_NTP_DEFAULT "pool.ntp.org"
#define APPCONF_TZ_DEFAULT  "UTC0"

typedef struct {
    char device_name[APPCONF_NAME_MAX + 1];
    char wifi_ssid[APPCONF_SSID_MAX + 1];
    char wifi_password[APPCONF_PASS_MAX + 1];
    char ntp_server[APPCONF_HOST_MAX + 1];
    char tz[APPCONF_TZ_MAX + 1]; /* POSIX TZ string, "MSK-3" */

    bool soft_switch; /* fade on and off instead of jumping */

    char hostname[APPCONF_HOST_MAX + 1];
} appconf_t;

void appconf_device_id(const uint8_t mac[6], char out[5]);

void appconf_default_name(const char device_id[5], char *out, size_t out_sz);

void appconf_defaults(appconf_t *c, const char device_id[5]);

void appconf_hostname(const char *device_name, const char device_id[5],
                      char *out, size_t out_sz);

void appconf_derive(appconf_t *c, const char device_id[5]);

bool appconf_validate(const appconf_t *c, char *err, size_t err_sz);

/* Resets every field group that fails validation to its default.
 * Returns the number of groups reset. */
int appconf_sanitize(appconf_t *c, const char device_id[5]);

#ifdef __cplusplus
}
#endif
