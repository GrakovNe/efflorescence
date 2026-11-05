/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "time_mgr.h"

#include <stdlib.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "time";

/* 2026-01-01: a clock before the firmware existed was never set. */
#define SANE_EPOCH 1767225600LL

static volatile bool s_synced;

static void on_sync(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "synced, unix time %lld", (long long)tv->tv_sec);
}

void time_mgr_start(const appconf_t *cfg)
{
    setenv("TZ", cfg->tz, 1);
    tzset();

    /* The RTC keeps counting through a restart, and the lamp may come
     * back without a network: the schedule should not wait for NTP. */
    time_t now = time(NULL);
    if ((int64_t)now >= SANE_EPOCH) {
        s_synced = true;
        ESP_LOGI(TAG, "clock kept across the restart, unix time %lld",
                 (long long)now);
    }

    if (cfg->ntp_server[0] == '\0') {
        ESP_LOGW(TAG, "no NTP server configured, time stays unsynced");
        return;
    }

    /* esp_netif_sntp keeps the server string by pointer; cfg is static. */
    esp_sntp_config_t scfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(cfg->ntp_server);
    scfg.sync_cb = on_sync;
    scfg.wait_for_sync = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&scfg));
    ESP_LOGI(TAG, "sntp started, server \"%s\", zone %s", cfg->ntp_server,
             cfg->tz);
}

bool time_mgr_synced(void)
{
    return s_synced;
}

int64_t time_mgr_unix(void)
{
    if (!s_synced) {
        return -1;
    }
    return (int64_t)time(NULL);
}

bool time_mgr_local(struct tm *out)
{
    if (!s_synced) {
        return false;
    }
    time_t now = time(NULL);
    localtime_r(&now, out);
    return true;
}
