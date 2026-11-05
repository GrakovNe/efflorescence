/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "appconf.h"
#include "lamp.h"
#include "mdns.h"
#include "reset.h"
#include "settings.h"
#include "time_mgr.h"
#include "touch.h"
#include "web_server.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

#ifndef APP_VER
#define APP_VER "dev"
#endif

static appconf_t s_cfg;

static void mdns_start(const appconf_t *cfg)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init: %s", esp_err_to_name(err));
        return;
    }
    err = mdns_hostname_set(cfg->hostname);
    if (err == ESP_OK) {
        err = mdns_instance_name_set(cfg->device_name);
    }
    if (err == ESP_OK) {
        err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns registration: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "mdns: http://%s.local/", cfg->hostname);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Efflorescence firmware %s (idf %s, built %s %s)",
             APP_VER, esp_get_idf_version(), __DATE__, __TIME__);

    reset_early_init();
    settings_nvs_init();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char device_id[5];
    appconf_device_id(mac, device_id);

    appconf_t *cfg = &s_cfg;
    settings_load(cfg, device_id);
    ESP_LOGI(TAG, "device %s: name \"%s\", hostname %s.local",
             device_id, cfg->device_name, cfg->hostname);
    ESP_LOGI(TAG, "wifi: %s, ntp: %s",
             cfg->wifi_ssid[0] != '\0' ? cfg->wifi_ssid : "not configured",
             cfg->ntp_server);

    /* The light and the sensor come first: the lamp works without a
     * network. */
    lamp_start(cfg);
    touch_start();

    wifi_mgr_start(cfg, device_id);
    web_server_start(cfg, device_id);
    time_mgr_start(cfg);
    mdns_start(cfg);
}
