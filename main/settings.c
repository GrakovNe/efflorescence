/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define STATE_NS "state"

#define KEY_ON  "on"
#define KEY_BRI "bri"

void settings_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs: %s, erasing once", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void settings_load_state(bool *on, uint8_t *brightness)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(STATE_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open %s: %s", STATE_NS, esp_err_to_name(err));
        return;
    }
    uint8_t u8;
    if (nvs_get_u8(h, KEY_ON, &u8) == ESP_OK) {
        *on = u8 != 0;
    }
    if (nvs_get_u8(h, KEY_BRI, &u8) == ESP_OK) {
        *brightness = u8;
    }
    nvs_close(h);
}

esp_err_t settings_save_state(bool on, uint8_t brightness)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(STATE_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open %s: %s", STATE_NS, esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u8(h, KEY_ON, on ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, KEY_BRI, brightness);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save state: %s", esp_err_to_name(err));
    }
    return err;
}
