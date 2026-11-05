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
#define SCHED_NS "sched"

#define KEY_NAME  "name"
#define KEY_WSSID "wssid"
#define KEY_WPASS "wpass"
#define KEY_NTP   "ntp"
#define KEY_TZ    "tz"
#define KEY_SOFT  "soft"
#define KEY_MQ_EN   "mq_en"
#define KEY_MQ_HOST "mq_host"
#define KEY_MQ_PORT "mq_port"
#define KEY_MQ_USER "mq_user"
#define KEY_MQ_PASS "mq_pass"
#define KEY_MQ_TLS  "mq_tls"
#define KEY_MQ_CA   "mq_ca"
#define KEY_MQ_SKIP "mq_skip"
#define KEY_MQ_PFX  "mq_pfx"
#define KEY_MQ_DISC "mq_disc"

#define KEY_ON  "on"
#define KEY_BRI "bri"

#define KEY_SCHED     "list"
#define SCHED_VERSION 1

typedef struct {
    uint8_t version;
    sched_t sched;
} sched_blob_t;

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

static void get_u16(nvs_handle_t h, const char *key, uint16_t *out)
{
    uint16_t u16;
    if (nvs_get_u16(h, key, &u16) == ESP_OK) {
        *out = u16;
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
    get_str(h, KEY_TZ, c->tz, sizeof c->tz);
    get_bool(h, KEY_SOFT, &c->soft_switch);
    get_bool(h, KEY_MQ_EN, &c->mqtt_enabled);
    get_str(h, KEY_MQ_HOST, c->mqtt_host, sizeof c->mqtt_host);
    get_u16(h, KEY_MQ_PORT, &c->mqtt_port);
    get_str(h, KEY_MQ_USER, c->mqtt_username, sizeof c->mqtt_username);
    get_str(h, KEY_MQ_PASS, c->mqtt_password, sizeof c->mqtt_password);
    get_bool(h, KEY_MQ_TLS, &c->mqtt_tls);
    get_str(h, KEY_MQ_CA, c->mqtt_ca_cert, sizeof c->mqtt_ca_cert);
    get_bool(h, KEY_MQ_SKIP, &c->mqtt_skip_verify);
    get_str(h, KEY_MQ_PFX, c->mqtt_prefix, sizeof c->mqtt_prefix);
    get_bool(h, KEY_MQ_DISC, &c->mqtt_discovery);
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
    PUT(nvs_set_str(h, KEY_TZ, c->tz));
    PUT(nvs_set_u8(h, KEY_SOFT, c->soft_switch));
    PUT(nvs_set_u8(h, KEY_MQ_EN, c->mqtt_enabled));
    PUT(nvs_set_str(h, KEY_MQ_HOST, c->mqtt_host));
    PUT(nvs_set_u16(h, KEY_MQ_PORT, c->mqtt_port));
    PUT(nvs_set_str(h, KEY_MQ_USER, c->mqtt_username));
    PUT(nvs_set_str(h, KEY_MQ_PASS, c->mqtt_password));
    PUT(nvs_set_u8(h, KEY_MQ_TLS, c->mqtt_tls));
    PUT(nvs_set_str(h, KEY_MQ_CA, c->mqtt_ca_cert));
    PUT(nvs_set_u8(h, KEY_MQ_SKIP, c->mqtt_skip_verify));
    PUT(nvs_set_str(h, KEY_MQ_PFX, c->mqtt_prefix));
    PUT(nvs_set_u8(h, KEY_MQ_DISC, c->mqtt_discovery));
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

void settings_load_schedule(sched_t *out)
{
    memset(out, 0, sizeof *out);
    nvs_handle_t h;
    if (nvs_open(SCHED_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    sched_blob_t blob;
    size_t sz = sizeof blob;
    esp_err_t err = nvs_get_blob(h, KEY_SCHED, &blob, &sz);
    nvs_close(h);
    char why[96];
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK || sz != sizeof blob || blob.version != SCHED_VERSION ||
        !sched_validate(&blob.sched, why, sizeof why)) {
        ESP_LOGW(TAG, "stored schedule unreadable, starting empty");
        return;
    }
    *out = blob.sched;
}

esp_err_t settings_save_schedule(const sched_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SCHED_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open %s: %s", SCHED_NS, esp_err_to_name(err));
        return err;
    }
    /* static: zeroed padding keeps the blob the same for the same list. */
    static sched_blob_t blob;
    memset(&blob, 0, sizeof blob);
    blob.version = SCHED_VERSION;
    blob.sched = *s;
    err = nvs_set_blob(h, KEY_SCHED, &blob, sizeof blob);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save schedule: %s", esp_err_to_name(err));
    }
    return err;
}
