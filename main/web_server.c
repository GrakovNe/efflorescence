/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_server.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "battery.h"
#include "captive.h"
#include "config_merge.h"
#include "lamp.h"
#include "light_cmd.h"
#include "logbuf.h"
#include "mqtt_mgr.h"
#include "ota.h"
#include "power.h"
#include "reset.h"
#include "sched_mgr.h"
#include "settings.h"
#include "time_mgr.h"
#include "touch.h"
#include "wifi_mgr.h"

#include "setup_html.h"
#include "main_html.h"
#include "info_html.h"
#include "schedule_html.h"

static const char *TAG = "web";

#define POST_BODY_MAX 8192
#define CMD_BODY_MAX  256
#define UPLOAD_CHUNK   4096

/* What the 80 MHz LEDC clock can divide down to at this resolution. */
#define PWM_FREQ_MIN_HZ 10
#define PWM_FREQ_MAX_HZ (80000000 >> CONFIG_APP_PWM_BITS)

static httpd_handle_t s_httpd;
static char s_device_id[5];
static char s_mac_str[18];
static const appconf_t *s_cfg;

/* Too big for the httpd stack; the server is single-threaded. */
static appconf_t s_merged;

static void add_str_or_null(cJSON *obj, const char *key, const char *val)
{
    if (val != NULL && val[0] != '\0') {
        cJSON_AddStringToObject(obj, key, val);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char *s = cJSON_PrintUnformatted(body);
    esp_err_t err = ESP_OK;
    if (s != NULL) {
        err = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
        cJSON_free(s);
    } else {
        err = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_Delete(body);
    return err;
}

static esp_err_t send_json_err(httpd_req_t *req, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddStringToObject(o, "error", msg);
    return send_json(req, "400 Bad Request", o);
}

static esp_err_t serve_gz(httpd_req_t *req, const unsigned char *gz,
                          unsigned int len)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)gz, len);
}

static int read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if ((size_t)req->content_len >= cap) {
        return -1;
    }
    size_t total = 0;
    while (total < (size_t)req->content_len) {
        int n = httpd_req_recv(req, buf + total, req->content_len - total);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }
    buf[total] = '\0';
    return (int)total;
}

static esp_err_t handler_main(httpd_req_t *req)
{
    return serve_gz(req, main_html_gz, main_html_gz_len);
}

static esp_err_t handler_settings_page(httpd_req_t *req)
{
    return serve_gz(req, setup_html_gz, setup_html_gz_len);
}

static esp_err_t handler_info(httpd_req_t *req)
{
    return serve_gz(req, info_html_gz, info_html_gz_len);
}

static esp_err_t handler_schedule_page(httpd_req_t *req)
{
    return serve_gz(req, schedule_html_gz, schedule_html_gz_len);
}

static esp_err_t handler_root(httpd_req_t *req)
{
    if (wifi_mgr_ap_up()) {
        return handler_settings_page(req);
    }
    return handler_main(req);
}

static esp_err_t handler_wifi_scan(httpd_req_t *req)
{
    char query[64] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "refresh", val, sizeof val) == ESP_OK) {
            wifi_mgr_scan_start();
        }
    }

    wifi_mgr_scan_entry_t nets[WIFI_MGR_SCAN_MAX];
    size_t n = wifi_mgr_scan_result(nets, WIFI_MGR_SCAN_MAX);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    if (root == NULL || arr == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            break;
        }
        cJSON_AddStringToObject(o, "ssid", nets[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", nets[i].rssi);
        cJSON_AddBoolToObject(o, "secure", nets[i].secure);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddBoolToObject(root, "scanning", wifi_mgr_scan_busy());
    return send_json(req, "200 OK", root);
}

static void add_timestamp(cJSON *obj, const char *key)
{
    int64_t ts = time_mgr_unix();
    if (ts < 0) {
        cJSON_AddNullToObject(obj, key);
    } else {
        cJSON_AddNumberToObject(obj, key, (double)ts);
    }
}

static esp_err_t send_light(httpd_req_t *req)
{
    lamp_status_t l;
    lamp_get(&l);
    cJSON *o = light_state_json(l.on, l.brightness, l.soft, battery_low());
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    return send_json(req, "200 OK", o);
}

static esp_err_t handler_light_get(httpd_req_t *req)
{
    return send_light(req);
}

static esp_err_t handler_light_post(httpd_req_t *req)
{
    char body[CMD_BODY_MAX];
    if (read_body(req, body, sizeof body) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return send_json_err(req, "body is not valid JSON");
    }
    light_cmd_t cmd;
    char err[96];
    bool ok = light_cmd_parse(root, &cmd, err, sizeof err);
    cJSON_Delete(root);
    if (!ok) {
        return send_json_err(req, err);
    }
    lamp_apply(&cmd);
    return send_light(req);
}

static void add_clock(cJSON *obj)
{
    struct tm tm;
    if (!time_mgr_local(&tm)) {
        cJSON_AddNullToObject(obj, "local");
        cJSON_AddNullToObject(obj, "weekday");
        return;
    }
    char local[20];
    strftime(local, sizeof local, "%Y-%m-%dT%H:%M:%S", &tm);
    cJSON_AddStringToObject(obj, "local", local);
    cJSON_AddNumberToObject(obj, "weekday", (tm.tm_wday + 6) % 7 + 1);
}

static esp_err_t send_schedule(httpd_req_t *req)
{
    sched_t s;
    sched_mgr_get(&s);
    sched_mgr_status_t st;
    sched_mgr_get_status(&st);

    cJSON *root = sched_to_json(&s);
    cJSON *clock = cJSON_AddObjectToObject(root, "clock");
    if (root == NULL || clock == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddNumberToObject(root, "max", SCHED_MAX);
    cJSON_AddBoolToObject(clock, "set", st.clock_set);
    cJSON_AddStringToObject(clock, "tz", s_cfg->tz);
    add_clock(clock);
    if (st.next >= 0) {
        cJSON *next = cJSON_AddObjectToObject(root, "next");
        if (next != NULL) {
            cJSON_AddNumberToObject(next, "entry", st.next);
            cJSON_AddNumberToObject(next, "in_minutes", st.next_in_min);
        }
    } else {
        cJSON_AddNullToObject(root, "next");
    }
    return send_json(req, "200 OK", root);
}

static esp_err_t handler_schedule_get(httpd_req_t *req)
{
    return send_schedule(req);
}

/* Replaces the whole list. It takes effect at once, no restart. */
static esp_err_t handler_schedule_post(httpd_req_t *req)
{
    char *body = malloc(POST_BODY_MAX);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    if (read_body(req, body, POST_BODY_MAX) < 0) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        return send_json_err(req, "body is not valid JSON");
    }
    sched_t s;
    char err[96];
    bool ok = sched_from_json(root, &s, err, sizeof err);
    cJSON_Delete(root);
    if (!ok) {
        return send_json_err(req, err);
    }
    if (sched_mgr_set(&s) != ESP_OK) {
        return send_json_err(req, "could not save the schedule");
    }
    return send_schedule(req);
}

static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power_on";
    case ESP_RST_EXT:      return "ext";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    case ESP_RST_USB:      return "usb";
    default:               return "unknown";
    }
}

static esp_err_t handler_status(httpd_req_t *req)
{
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);
    lamp_status_t l;
    lamp_get(&l);

    cJSON *root = cJSON_CreateObject();
    cJSON *tm = cJSON_AddObjectToObject(root, "time");
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *light = light_state_json(l.on, l.brightness, l.soft, battery_low());
    cJSON *pwm = cJSON_CreateObject();
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    if (root == NULL || tm == NULL || dev == NULL || light == NULL ||
        pwm == NULL || wifi == NULL || mqtt == NULL) {
        cJSON_Delete(light);
        cJSON_Delete(pwm);
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddItemToObject(root, "light", light);
    cJSON_AddItemToObject(light, "pwm", pwm);

    add_timestamp(tm, "timestamp");
    cJSON_AddBoolToObject(tm, "synced", time_mgr_synced());
    cJSON_AddStringToObject(tm, "tz", s_cfg->tz);
    add_clock(tm);
    cJSON_AddNumberToObject(tm, "uptime_s", esp_timer_get_time() / 1000000);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char rev[12];
    snprintf(rev, sizeof rev, "v%u.%u",
             (unsigned)chip.revision / 100, (unsigned)chip.revision % 100);
    uint32_t flash_sz = 0;
    if (esp_flash_get_size(NULL, &flash_sz) != ESP_OK) {
        flash_sz = 0;
    }
    cJSON_AddStringToObject(dev, "name", s_cfg->device_name);
    cJSON_AddStringToObject(dev, "id", s_device_id);
    cJSON_AddStringToObject(dev, "hostname", s_cfg->hostname);
    cJSON_AddStringToObject(dev, "chip", "ESP32-C3");
    cJSON_AddStringToObject(dev, "revision", rev);
    cJSON_AddNumberToObject(dev, "cores", chip.cores);
    cJSON_AddNumberToObject(dev, "cpu_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    cJSON_AddStringToObject(dev, "firmware", esp_app_get_description()->version);
    ota_status_t ota;
    ota_get_status(&ota);
    cJSON_AddStringToObject(dev, "slot", ota.slot);
    cJSON_AddBoolToObject(dev, "pending_verify", ota.pending_verify);
    cJSON_AddStringToObject(dev, "idf", esp_get_idf_version());
    cJSON_AddStringToObject(dev, "build_date", __DATE__);
    cJSON_AddStringToObject(dev, "mac", s_mac_str);
    if (flash_sz != 0) {
        cJSON_AddNumberToObject(dev, "flash_bytes", flash_sz);
    } else {
        cJSON_AddNullToObject(dev, "flash_bytes");
    }
    cJSON_AddNumberToObject(dev, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(dev, "heap_min_free",
                            esp_get_minimum_free_heap_size());
    cJSON_AddStringToObject(dev, "reset_reason", reset_reason_str());

    cJSON_AddStringToObject(root, "mode", wifi_mgr_mode_name(w.mode));

    cJSON_AddNumberToObject(pwm, "freq_hz", l.freq_hz);
    cJSON_AddNumberToObject(pwm, "bits", CONFIG_APP_PWM_BITS);
    cJSON_AddNumberToObject(pwm, "duty", l.duty);
    cJSON_AddNumberToObject(pwm, "duty_min", l.duty_min);
    cJSON_AddNumberToObject(pwm, "duty_max", l.duty_max);
    cJSON_AddBoolToObject(pwm, "raw", l.raw);
    cJSON_AddBoolToObject(pwm, "steady", l.steady);

    cJSON_AddBoolToObject(wifi, "connected", w.sta_connected);
    cJSON_AddBoolToObject(wifi, "ap_up", w.ap_up);
    add_str_or_null(wifi, "ssid", w.ssid);
    add_str_or_null(wifi, "bssid", w.bssid);
    if (w.sta_connected) {
        cJSON_AddNumberToObject(wifi, "rssi", w.rssi);
        cJSON_AddNumberToObject(wifi, "channel", w.channel);
        add_str_or_null(wifi, "ip", w.ip);
        add_str_or_null(wifi, "gateway", w.gateway);
    } else {
        cJSON_AddNullToObject(wifi, "rssi");
        cJSON_AddNullToObject(wifi, "channel");
        cJSON_AddNullToObject(wifi, "ip");
        cJSON_AddNullToObject(wifi, "gateway");
    }
    add_str_or_null(wifi, "last_disconnect", w.last_disconnect);

    cJSON *touch = cJSON_AddObjectToObject(root, "touch");
    if (touch != NULL) {
        cJSON_AddNumberToObject(touch, "stuck", touch_stuck_count());
        cJSON_AddBoolToObject(touch, "trace", touch_trace());
    }

    mqtt_mgr_status_t mq;
    mqtt_mgr_get_status(&mq);
    cJSON_AddBoolToObject(mqtt, "enabled", mq.enabled);
    cJSON_AddBoolToObject(mqtt, "connected", mq.connected);
    if (mq.last_publish > 0) {
        cJSON_AddNumberToObject(mqtt, "last_publish", (double)mq.last_publish);
    } else {
        cJSON_AddNullToObject(mqtt, "last_publish");
    }
    cJSON_AddNumberToObject(mqtt, "errors", mq.errors);

    return send_json(req, "200 OK", root);
}

static esp_err_t handler_config_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *time = cJSON_AddObjectToObject(root, "time");
    cJSON *light = cJSON_AddObjectToObject(root, "light");
    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    if (root == NULL || dev == NULL || wifi == NULL || time == NULL ||
        light == NULL || mqtt == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddStringToObject(dev, "name", s_cfg->device_name);
    cJSON_AddStringToObject(wifi, "ssid", s_cfg->wifi_ssid);
    cJSON_AddBoolToObject(wifi, "password_set", s_cfg->wifi_password[0] != '\0');
    cJSON_AddStringToObject(time, "ntp_server", s_cfg->ntp_server);
    cJSON_AddStringToObject(time, "tz", s_cfg->tz);
    cJSON_AddBoolToObject(light, "soft", s_cfg->soft_switch);
    cJSON_AddBoolToObject(mqtt, "enabled", s_cfg->mqtt_enabled);
    cJSON_AddStringToObject(mqtt, "host", s_cfg->mqtt_host);
    cJSON_AddNumberToObject(mqtt, "port", s_cfg->mqtt_port);
    cJSON_AddStringToObject(mqtt, "username", s_cfg->mqtt_username);
    cJSON_AddBoolToObject(mqtt, "password_set", s_cfg->mqtt_password[0] != '\0');
    cJSON_AddBoolToObject(mqtt, "tls", s_cfg->mqtt_tls);
    cJSON_AddBoolToObject(mqtt, "ca_cert_set", s_cfg->mqtt_ca_cert[0] != '\0');
    cJSON_AddBoolToObject(mqtt, "skip_verify", s_cfg->mqtt_skip_verify);
    cJSON_AddStringToObject(mqtt, "topic_prefix", s_cfg->mqtt_prefix);
    cJSON_AddBoolToObject(mqtt, "discovery", s_cfg->mqtt_discovery);
    return send_json(req, "200 OK", root);
}

#define RESTART_DELAY_US (1000 * 1000)

static void restart_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "restarting");
    esp_restart();
}

/* Restart after the response has left, not inside the handler. */
static void schedule_restart(void)
{
    lamp_flush();
    const esp_timer_create_args_t args = {
        .callback = restart_timer_cb,
        .name = "restart",
    };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, RESTART_DELAY_US);
    } else {
        esp_restart();
    }
}

static esp_err_t handler_config_post(httpd_req_t *req)
{
    char *body = malloc(POST_BODY_MAX);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    int len = read_body(req, body, POST_BODY_MAX);
    if (len < 0) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        return send_json_err(req, "body is not valid JSON");
    }

    /* NVS, not s_cfg: what is stored is the base for the merge. */
    appconf_t *m = &s_merged;
    settings_load(m, s_device_id);
    char err[160];
    bool ok = config_merge(root, m, err, sizeof err);
    cJSON_Delete(root);
    if (!ok) {
        return send_json_err(req, err);
    }
    if (settings_save(m) != ESP_OK) {
        return send_json_err(req, "could not save settings");
    }

    appconf_derive(m, s_device_id);
    cJSON *out = cJSON_CreateObject();
    if (out == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    char url[APPCONF_HOST_MAX + 32];
    snprintf(url, sizeof url, "http://%s.local/", m->hostname);
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "url", url);
    ESP_LOGI(TAG, "settings saved, restarting soon");
    esp_err_t r = send_json(req, "200 OK", out);
    schedule_restart();
    return r;
}

static void drain_body(httpd_req_t *req)
{
    char scratch[64];
    int remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, scratch,
                               remaining < (int)sizeof scratch
                                   ? remaining : (int)sizeof scratch);
        if (n <= 0) {
            return;
        }
        remaining -= n;
    }
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, "200 OK", o);
}

static esp_err_t handler_options(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods",
                       "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handler_factory_reset(httpd_req_t *req)
{
    drain_body(req);
    ESP_LOGW(TAG, "factory reset requested over HTTP");
    esp_err_t r = send_ok(req);
    reset_mark_factory();
    schedule_restart();
    return r;
}

static esp_err_t handler_reboot(httpd_req_t *req)
{
    drain_body(req);
    ESP_LOGW(TAG, "reboot requested over HTTP");
    esp_err_t r = send_ok(req);
    schedule_restart();
    return r;
}

static esp_err_t send_ota(httpd_req_t *req, const char *status)
{
    ota_status_t s;
    ota_get_status(&s);
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddStringToObject(o, "current", s.current);
    cJSON_AddStringToObject(o, "slot", s.slot);
    cJSON_AddBoolToObject(o, "pending_verify", s.pending_verify);
    cJSON_AddStringToObject(o, "phase", ota_phase_name(s.phase));
    if (s.progress >= 0) {
        cJSON_AddNumberToObject(o, "progress", s.progress);
    } else {
        cJSON_AddNullToObject(o, "progress");
    }
    add_str_or_null(o, "available", s.available);
    cJSON_AddBoolToObject(o, "newer", s.newer);
    add_str_or_null(o, "error", s.error);
    cJSON_AddStringToObject(o, "manifest", CONFIG_APP_UPDATE_URL);
    return send_json(req, status, o);
}

static esp_err_t handler_ota_get(httpd_req_t *req)
{
    return send_ota(req, "200 OK");
}

/* An empty body checks the configured manifest; {"url": "..."} checks
 * another one, which is how an update is tried before it is released. */
static esp_err_t handler_ota_check(httpd_req_t *req)
{
    char body[CMD_BODY_MAX + OTA_URL_MAX];
    if (read_body(req, body, sizeof body) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    char url[OTA_URL_MAX + 1] = "";
    if (body[0] != '\0') {
        cJSON *root = cJSON_Parse(body);
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, "url");
        bool bad = !cJSON_IsObject(root) ||
                   (u != NULL && (!cJSON_IsString(u) ||
                                  strlen(u->valuestring) > OTA_URL_MAX));
        if (!bad && u != NULL) {
            strlcpy(url, u->valuestring, sizeof url);
        }
        cJSON_Delete(root);
        if (bad) {
            return send_json_err(req, "\"url\" must be a string of a sane length");
        }
    }
    if (!ota_check(url)) {
        return send_json_err(req, "another update operation is running");
    }
    return send_ota(req, "200 OK");
}

static esp_err_t handler_ota_install(httpd_req_t *req)
{
    drain_body(req);
    if (!ota_install()) {
        return send_json_err(req, "nothing checked yet, or another update "
                                  "operation is running");
    }
    return send_ota(req, "200 OK");
}

/* The image is the request body:
 *   curl --data-binary @build/efflorescence.bin http://<host>/api/v1/ota */
static esp_err_t handler_ota_upload(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        return send_json_err(req, "send the image as the request body");
    }
    if (ota_upload_begin(req->content_len) != ESP_OK) {
        drain_body(req);
        return send_ota(req, "400 Bad Request");
    }
    char *buf = malloc(UPLOAD_CHUNK);
    if (buf == NULL) {
        ota_upload_abort();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    int remaining = req->content_len;
    bool failed = false;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining < UPLOAD_CHUNK ? remaining
                                                                 : UPLOAD_CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            if (!failed) {
                ota_upload_abort();
            }
            free(buf);
            return ESP_FAIL;
        }
        remaining -= n;
        /* After a failure the rest is still read, so the answer arrives. */
        if (!failed && ota_upload_write(buf, (size_t)n) != ESP_OK) {
            failed = true;
        }
    }
    free(buf);
    if (failed || ota_upload_end() != ESP_OK) {
        return send_ota(req, "400 Bad Request");
    }
    esp_err_t r = send_ota(req, "200 OK");
    schedule_restart();
    return r;
}

static esp_err_t send_text(httpd_req_t *req, size_t (*fill)(char *, size_t),
                           size_t cap)
{
    char *buf = malloc(cap);
    if (buf == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    size_t n = fill(buf, cap);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, buf, (ssize_t)n);
    free(buf);
    return err;
}

/* The last few kilobytes of the console. */
static esp_err_t handler_log(httpd_req_t *req)
{
    return send_text(req, logbuf_read, LOGBUF_SIZE);
}

/* Bench tool: power management locks and the time spent asleep. */
static esp_err_t handler_debug_pm(httpd_req_t *req)
{
    return send_text(req, power_dump, 2048);
}

/* Bench tool: {"simulate": "brownout" | "sleep" | "clear"}. */
static esp_err_t handler_debug_battery(httpd_req_t *req)
{
    char body[CMD_BODY_MAX];
    if (read_body(req, body, sizeof body) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *what = cJSON_GetObjectItemCaseSensitive(root, "simulate");
    char word[16] = "";
    if (cJSON_IsString(what)) {
        strlcpy(word, what->valuestring, sizeof word);
    }
    cJSON_Delete(root);
    bool restarts = strcmp(word, "brownout") == 0 || strcmp(word, "sleep") == 0;
    if (!restarts && strcmp(word, "clear") != 0) {
        return send_json_err(req, "\"simulate\" must be \"brownout\", "
                                  "\"sleep\" or \"clear\"");
    }
    ESP_LOGW(TAG, "bench battery: %s", word);
    esp_err_t r = send_ok(req);
    if (restarts) {
        /* Let the answer leave first. */
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    battery_simulate(word);
    return r;
}

/* Bench tool: {"trace": true | false} logs every move of the sensor
 * output; {"reset": true} cuts the sensor's power for a moment. */
static esp_err_t handler_debug_touch(httpd_req_t *req)
{
    char body[CMD_BODY_MAX];
    if (read_body(req, body, sizeof body) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *tr = cJSON_GetObjectItemCaseSensitive(root, "trace");
    const cJSON *rs = cJSON_GetObjectItemCaseSensitive(root, "reset");
    if (cJSON_IsBool(tr)) {
        touch_set_trace(cJSON_IsTrue(tr));
    }
    if (cJSON_IsTrue(rs)) {
        touch_reset_sensor();
    }
    cJSON_Delete(root);
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddBoolToObject(o, "trace", touch_trace());
    return send_json(req, "200 OK", o);
}

/* Bench tool: {"duty": 0..duty_max, "freq_hz": n} drives the LED raw,
 * {"duty": null} hands it back to the lamp. */
static esp_err_t handler_debug_pwm(httpd_req_t *req)
{
    char body[CMD_BODY_MAX];
    if (read_body(req, body, sizeof body) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }
    cJSON *root = cJSON_Parse(body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_json_err(req, "body is not a JSON object");
    }
    const cJSON *duty = cJSON_GetObjectItemCaseSensitive(root, "duty");
    const cJSON *freq = cJSON_GetObjectItemCaseSensitive(root, "freq_hz");
    lamp_status_t l;
    lamp_get(&l);

    const char *bad = NULL;
    int32_t duty_v = -1;
    uint32_t freq_v = 0;
    if (duty != NULL && !cJSON_IsNull(duty)) {
        if (!cJSON_IsNumber(duty) || duty->valuedouble < 0 ||
            duty->valuedouble > l.duty_max ||
            duty->valuedouble != floor(duty->valuedouble)) {
            bad = "\"duty\" must be null or an integer within 0..duty_max";
        } else {
            duty_v = (int32_t)duty->valuedouble;
        }
    }
    if (bad == NULL && freq != NULL) {
        if (!cJSON_IsNumber(freq) || freq->valuedouble < PWM_FREQ_MIN_HZ ||
            freq->valuedouble > PWM_FREQ_MAX_HZ ||
            freq->valuedouble != floor(freq->valuedouble)) {
            bad = "\"freq_hz\" is out of the range this resolution allows";
        } else {
            freq_v = (uint32_t)freq->valuedouble;
        }
    }
    cJSON_Delete(root);
    if (bad != NULL) {
        return send_json_err(req, bad);
    }
    ESP_LOGW(TAG, "bench pwm: duty %" PRId32 ", freq %" PRIu32, duty_v, freq_v);
    lamp_debug_pwm(duty_v, freq_v);
    return send_ok(req);
}

static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    if (wifi_mgr_ap_up()) {
        static const char body[] =
            "<html><head><meta http-equiv=\"refresh\" content=\"0;url=" CAPTIVE_URL "\"></head>"
            "<body>Setting up your device: <a href=\"" CAPTIVE_URL "\">Efflorescence setup</a></body></html>";
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", CAPTIVE_URL);
        httpd_resp_set_type(req, "text/html; charset=utf-8");

        return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "not found", HTTPD_RESP_USE_STRLEN);
}

bool web_server_up(void)
{
    return s_httpd != NULL;
}

void web_server_start(const appconf_t *cfg, const char device_id[5])
{
    s_cfg = cfg;
    snprintf(s_device_id, sizeof s_device_id, "%s", device_id);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_mac_str, sizeof s_mac_str, MACSTR, MAC2STR(mac));

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 48;
    hc.stack_size = 8192;
    hc.lru_purge_enable = true;
    hc.server_port = 80;

    ESP_ERROR_CHECK(httpd_start(&s_httpd, &hc));

    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handler_root },
        { .uri = "/main", .method = HTTP_GET, .handler = handler_main },
        { .uri = "/config", .method = HTTP_GET, .handler = handler_settings_page },
        { .uri = "/info", .method = HTTP_GET, .handler = handler_info },
        { .uri = "/schedule", .method = HTTP_GET, .handler = handler_schedule_page },
        { .uri = "/api/v1/schedule", .method = HTTP_GET, .handler = handler_schedule_get },
        { .uri = "/api/v1/schedule", .method = HTTP_POST, .handler = handler_schedule_post },
        { .uri = "/api/v1/schedule", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = handler_wifi_scan },
        { .uri = "/api/v1/light", .method = HTTP_GET, .handler = handler_light_get },
        { .uri = "/api/v1/light", .method = HTTP_POST, .handler = handler_light_post },
        { .uri = "/api/v1/light", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/status", .method = HTTP_GET, .handler = handler_status },
        { .uri = "/api/v1/config", .method = HTTP_GET, .handler = handler_config_get },
        { .uri = "/api/v1/config", .method = HTTP_POST, .handler = handler_config_post },
        { .uri = "/api/v1/config", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/factory-reset", .method = HTTP_POST, .handler = handler_factory_reset },
        { .uri = "/api/v1/factory-reset", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/reboot", .method = HTTP_POST, .handler = handler_reboot },
        { .uri = "/api/v1/reboot", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/ota", .method = HTTP_GET, .handler = handler_ota_get },
        { .uri = "/api/v1/ota", .method = HTTP_POST, .handler = handler_ota_upload },
        { .uri = "/api/v1/ota", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/ota/check", .method = HTTP_POST, .handler = handler_ota_check },
        { .uri = "/api/v1/ota/check", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/ota/install", .method = HTTP_POST, .handler = handler_ota_install },
        { .uri = "/api/v1/ota/install", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/log", .method = HTTP_GET, .handler = handler_log },
        { .uri = "/api/v1/debug/pm", .method = HTTP_GET, .handler = handler_debug_pm },
        { .uri = "/api/v1/debug/battery", .method = HTTP_POST, .handler = handler_debug_battery },
        { .uri = "/api/v1/debug/battery", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/debug/touch", .method = HTTP_POST, .handler = handler_debug_touch },
        { .uri = "/api/v1/debug/pwm", .method = HTTP_POST, .handler = handler_debug_pwm },
        { .uri = "/api/v1/debug/pwm", .method = HTTP_OPTIONS, .handler = handler_options },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &uris[i]));
    }
    ESP_ERROR_CHECK(httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND,
                                               handler_404));

    ESP_LOGI(TAG, "http server up on :80");
}
