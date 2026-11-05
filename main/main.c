/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "appconf.h"
#include "battery.h"
#include "lamp.h"
#include "logbuf.h"
#include "mdns.h"
#include "mqtt_mgr.h"
#include "ota.h"
#include "power.h"
#include "reset.h"
#include "sched_mgr.h"
#include "settings.h"
#include "time_mgr.h"
#include "touch.h"
#include "web_server.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

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
    logbuf_start();
    ESP_LOGI(TAG, "Efflorescence firmware %s (idf %s, built %s %s)",
             esp_app_get_description()->version, esp_get_idf_version(),
             __DATE__, __TIME__);

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
    /* Before anything drives a pin or starts the radio: on a battery
     * this may be where the lamp goes back to sleep. */
    battery_early_init(cfg->battery);
    ESP_LOGI(TAG, "power: %s", cfg->battery ? "battery" : "no battery");
    ESP_LOGI(TAG, "wifi: %s, mqtt: %s, ntp: %s, zone %s",
             cfg->wifi_ssid[0] != '\0' ? cfg->wifi_ssid : "not configured",
             cfg->mqtt_enabled ? cfg->mqtt_host : "disabled",
             cfg->ntp_server, cfg->tz);

    /* The light and the sensor come first: the lamp works without a
     * network. */
    power_start(cfg->battery);
    lamp_start(cfg);
    touch_start();
    battery_start();

    wifi_mgr_start(cfg, device_id);
    web_server_start(cfg, device_id);
    time_mgr_start(cfg);
    sched_mgr_start();
    mdns_start(cfg);
    ota_start(cfg);
    mqtt_mgr_start(cfg, device_id);
}
