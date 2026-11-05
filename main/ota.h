/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "appconf.h"
#include "ota_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_ERR_MAX 95

typedef enum {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_UPLOADING,
    OTA_RESTARTING,
} ota_phase_t;

typedef struct {
    ota_phase_t phase;
    int  progress;                       /* percent, -1 when unknown */
    char current[OTA_VERSION_MAX + 1];
    char available[OTA_VERSION_MAX + 1]; /* empty until a check succeeds */
    bool newer;
    char error[OTA_ERR_MAX + 1];         /* what the last operation died of */
    bool pending_verify;                 /* this image is still on probation */
    char slot[17];
} ota_status_t;

void ota_start(const appconf_t *cfg);

void ota_get_status(ota_status_t *out);

const char *ota_phase_name(ota_phase_t phase);

/* Both return false when another operation is running. url NULL means
 * the configured manifest. */
bool ota_check(const char *url);
bool ota_install(void);

/* A firmware image pushed to the device, in pieces. After a failed call
 * the upload is over; the reason is in the status. */
esp_err_t ota_upload_begin(size_t size);
esp_err_t ota_upload_write(const void *data, size_t len);
esp_err_t ota_upload_end(void);
void ota_upload_abort(void);

#ifdef __cplusplus
}
#endif
