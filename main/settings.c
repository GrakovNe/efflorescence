/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "settings.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define CFG_NS   "cfg"
#define STATE_NS "state"

#define KEY_NAME  "name"
#define KEY_WSSID "wssid"
#define KEY_WPASS "wpass"
#define KEY_NTP   "ntp"
#define KEY_SOFT  "soft"

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

/* nvs_get_str() fills the buffer before it checks the data CRC, so a
 * corrupt entry read straight into *out would wipe the default. */
static void get_str(nvs_handle_t h, const char *key, char *out, size_t out_sz)
{
    char *tmp = malloc(out_sz);
    if (tmp == NULL) {
        ESP_LOGW(TAG, "key %s: no memory, using default", key);
        return;
    }
    size_t sz = out_sz;
    esp_err_t err = nvs_get_str(h, key, tmp, &sz);
    if (err == ESP_OK && sz > 0) {
        memcpy(out, tmp, sz);
        out[sz - 1] = '\0';
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "key %s: %s, using default", key, esp_err_to_name(err));
    }
    free(tmp);
}

static void get_bool(nvs_handle_t h, const char *key, bool *out)
{
    uint8_t u8;
    if (nvs_get_u8(h, key, &u8) == ESP_OK) {
        *out = u8 != 0;
    }
}

void settings_load(appconf_t *c, const char device_id[5])
{
    appconf_defaults(c, device_id);

    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no saved settings, using factory defaults");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s, using factory defaults", esp_err_to_name(err));
        return;
    }

    get_str(h, KEY_NAME, c->device_name, sizeof c->device_name);
    get_str(h, KEY_WSSID, c->wifi_ssid, sizeof c->wifi_ssid);
    get_str(h, KEY_WPASS, c->wifi_password, sizeof c->wifi_password);
    get_str(h, KEY_NTP, c->ntp_server, sizeof c->ntp_server);
    get_bool(h, KEY_SOFT, &c->soft_switch);
    nvs_close(h);

    appconf_derive(c, device_id);
    int reset = appconf_sanitize(c, device_id);
    if (reset > 0) {
        ESP_LOGW(TAG, "stored settings invalid, %d group(s) reset to defaults",
                 reset);
    }
}

esp_err_t settings_save(const appconf_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

#define PUT(call)                  \
    do {                           \
        if (err == ESP_OK) {       \
            err = (call);          \
        }                          \
    } while (0)

    PUT(nvs_set_str(h, KEY_NAME, c->device_name));
    PUT(nvs_set_str(h, KEY_WSSID, c->wifi_ssid));
    PUT(nvs_set_str(h, KEY_WPASS, c->wifi_password));
    PUT(nvs_set_str(h, KEY_NTP, c->ntp_server));
    PUT(nvs_set_u8(h, KEY_SOFT, c->soft_switch));
    PUT(nvs_commit(h));
#undef PUT

    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t settings_save_soft(bool soft)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_u8(h, KEY_SOFT, soft ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save soft: %s", esp_err_to_name(err));
    }
    return err;
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
