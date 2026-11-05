/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ota.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "battery.h"
#include "lamp.h"
#include "web_server.h"
#include "wifi_mgr.h"

static const char *TAG = "ota";

#define MANIFEST_MAX     1024
#define HTTP_TIMEOUT_MS  15000
#define RESTART_DELAY_MS 1500
#define HEALTH_POLL_MS   1000

/* The lamp looks for a release by itself so that Home Assistant can
 * offer it; nobody is told when such a check fails. */
#define AUTO_CHECK_FIRST_MS (2 * 60 * 1000)
#define AUTO_CHECK_RETRY_MS (10 * 60 * 1000)
#define AUTO_CHECK_EVERY_MS (24 * 60 * 60 * 1000)

/* Where the application description sits in an image file. */
#define DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))
#define HEAD_SIZE   (DESC_OFFSET + sizeof(esp_app_desc_t))

typedef enum {
    CMD_CHECK,
    CMD_INSTALL,
} cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    char url[OTA_URL_MAX + 1];
} cmd_t;

static struct {
    const appconf_t *cfg;
    SemaphoreHandle_t lock;
    QueueHandle_t queue;

    ota_phase_t phase;
    int progress;
    char error[OTA_ERR_MAX + 1];
    ota_manifest_t manifest;
    bool newer;
    bool pending_verify;

    esp_ota_handle_t upload;
    const esp_partition_t *upload_part;
    size_t upload_size;
    size_t upload_done;
    uint8_t head[HEAD_SIZE];
    size_t head_len;
} ctx;

static const char *current_version(void)
{
    return esp_app_get_description()->version;
}

const char *ota_phase_name(ota_phase_t phase)
{
    switch (phase) {
    case OTA_IDLE:        return "idle";
    case OTA_CHECKING:    return "checking";
    case OTA_DOWNLOADING: return "downloading";
    case OTA_UPLOADING:   return "uploading";
    case OTA_RESTARTING:  return "restarting";
    }
    return "?";
}

static bool claim(ota_phase_t phase)
{
    bool ok = false;
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    if (ctx.phase == OTA_IDLE) {
        ctx.phase = phase;
        ctx.progress = -1;
        ctx.error[0] = '\0';
        ok = true;
    }
    xSemaphoreGive(ctx.lock);
    return ok;
}

static void set_phase(ota_phase_t phase)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    ctx.phase = phase;
    xSemaphoreGive(ctx.lock);
}

static void set_progress(size_t done, size_t total)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    ctx.progress = total > 0 ? (int)(done * 100 / total) : -1;
    xSemaphoreGive(ctx.lock);
}

static void fail(const char *fmt, ...)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx.error, sizeof ctx.error, fmt, ap);
    va_end(ap);
    ctx.phase = OTA_IDLE;
    ctx.progress = -1;
    ESP_LOGW(TAG, "%s", ctx.error);
    xSemaphoreGive(ctx.lock);
}

static void restart_soon(void)
{
    set_phase(OTA_RESTARTING);
    lamp_flush();
    ESP_LOGW(TAG, "new image in place, restarting");
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
}

/* A foreign image for the same chip would boot, never confirm itself
 * and, if it does not crash, never roll back. */
static bool desc_is_ours(const esp_app_desc_t *desc)
{
    return desc->magic_word == ESP_APP_DESC_MAGIC_WORD &&
           strncmp(desc->project_name, esp_app_get_description()->project_name,
                   sizeof desc->project_name) == 0;
}

static esp_err_t fetch(const char *url, char *buf, size_t cap, int *status)
{
    esp_http_client_config_t hc = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (c == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_open(c, 0);
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(c) < 0) {
            err = ESP_FAIL;
        } else {
            *status = esp_http_client_get_status_code(c);
            int n = esp_http_client_read_response(c, buf, (int)cap - 1);
            if (n < 0) {
                err = ESP_FAIL;
            } else {
                buf[n] = '\0';
            }
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

static void do_check(const char *url)
{
    char *body = malloc(MANIFEST_MAX);
    if (body == NULL) {
        fail("out of memory");
        return;
    }
    int status = 0;
    esp_err_t err = fetch(url, body, MANIFEST_MAX, &status);
    if (err != ESP_OK) {
        fail("manifest: %s", esp_err_to_name(err));
    } else if (status != 200) {
        fail("manifest: HTTP %d", status);
    } else {
        cJSON *root = cJSON_Parse(body);
        ota_manifest_t m;
        char why[OTA_ERR_MAX + 1];
        if (root == NULL) {
            fail("the manifest is not JSON");
        } else if (!ota_manifest_parse(root, url, &m, why, sizeof why)) {
            fail("%s", why);
        } else {
            xSemaphoreTake(ctx.lock, portMAX_DELAY);
            ctx.manifest = m;
            ctx.newer = ota_version_newer(m.version, current_version());
            ctx.phase = OTA_IDLE;
            ESP_LOGI(TAG, "running %s, published %s%s", current_version(),
                     m.version, ctx.newer ? " (newer)" : "");
            xSemaphoreGive(ctx.lock);
        }
        cJSON_Delete(root);
    }
    free(body);
}

static void do_install(const char *url)
{
    esp_http_client_config_t hc = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t oc = { .http_config = &hc };
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&oc, &h);
    if (err != ESP_OK) {
        fail("download: %s", esp_err_to_name(err));
        return;
    }
    esp_app_desc_t desc;
    err = esp_https_ota_get_img_desc(h, &desc);
    if (err != ESP_OK || !desc_is_ours(&desc)) {
        esp_https_ota_abort(h);
        fail("not an Efflorescence image");
        return;
    }
    ESP_LOGI(TAG, "downloading %s from %s", desc.version, url);
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int total = esp_https_ota_get_image_size(h);
        set_progress((size_t)esp_https_ota_get_image_len_read(h),
                     total > 0 ? (size_t)total : 0);
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        fail("download: %s", err != ESP_OK ? esp_err_to_name(err) : "cut short");
        return;
    }
    err = esp_https_ota_finish(h);
    if (err != ESP_OK) {
        fail("image rejected: %s", esp_err_to_name(err));
        return;
    }
    restart_soon();
}

static void ota_task(void *arg)
{
    (void)arg;
    TickType_t wait = pdMS_TO_TICKS(AUTO_CHECK_FIRST_MS);
    for (;;) {
        cmd_t cmd;
        if (xQueueReceive(ctx.queue, &cmd, wait) != pdTRUE) {
            wifi_mgr_status_t w;
            wifi_mgr_get_status(&w);
            wait = pdMS_TO_TICKS(AUTO_CHECK_RETRY_MS);
            if (w.sta_connected && claim(OTA_CHECKING)) {
                do_check(CONFIG_APP_UPDATE_URL);
                xSemaphoreTake(ctx.lock, portMAX_DELAY);
                ctx.error[0] = '\0';
                xSemaphoreGive(ctx.lock);
                wait = pdMS_TO_TICKS(AUTO_CHECK_EVERY_MS);
            }
            continue;
        }
        if (cmd.kind == CMD_CHECK) {
            do_check(cmd.url);
        } else {
            do_install(cmd.url);
        }
    }
}

static bool submit(cmd_kind_t kind, ota_phase_t phase, const char *url)
{
    /* Not on the stack of whoever calls: the URL makes it big. */
    static cmd_t cmd;
    if (!claim(phase)) {
        return false;
    }
    cmd.kind = kind;
    strlcpy(cmd.url, url, sizeof cmd.url);
    if (xQueueSend(ctx.queue, &cmd, 0) != pdTRUE) {
        fail("update task is busy");
        return false;
    }
    return true;
}

bool ota_check(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        url = CONFIG_APP_UPDATE_URL;
    }
    if (strlen(url) > OTA_URL_MAX) {
        return false;
    }
    return submit(CMD_CHECK, OTA_CHECKING, url);
}

/* Writing an image with the supply about to collapse is asking for it. */
static bool battery_refuses(void)
{
    if (!battery_low()) {
        return false;
    }
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    if (ctx.phase == OTA_IDLE) {
        strlcpy(ctx.error, "the battery is low", sizeof ctx.error);
    }
    xSemaphoreGive(ctx.lock);
    return true;
}

bool ota_install(void)
{
    if (battery_refuses()) {
        return false;
    }
    char url[OTA_URL_MAX + 1];
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    strlcpy(url, ctx.manifest.url, sizeof url);
    xSemaphoreGive(ctx.lock);
    if (url[0] == '\0') {
        return false;
    }
    return submit(CMD_INSTALL, OTA_DOWNLOADING, url);
}

esp_err_t ota_upload_begin(size_t size)
{
    if (battery_refuses()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!claim(OTA_UPLOADING)) {
        return ESP_ERR_INVALID_STATE;
    }
    ctx.upload_part = esp_ota_get_next_update_partition(NULL);
    if (ctx.upload_part == NULL) {
        fail("no update partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (size < HEAD_SIZE || size > ctx.upload_part->size) {
        fail("image size %u does not fit the %" PRIu32 "-byte slot",
             (unsigned)size, ctx.upload_part->size);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_ota_begin(ctx.upload_part, OTA_WITH_SEQUENTIAL_WRITES,
                                  &ctx.upload);
    if (err != ESP_OK) {
        fail("upload: %s", esp_err_to_name(err));
        return err;
    }
    ctx.upload_size = size;
    ctx.upload_done = 0;
    ctx.head_len = 0;
    ESP_LOGI(TAG, "upload of %u bytes into %s", (unsigned)size,
             ctx.upload_part->label);
    return ESP_OK;
}

static esp_err_t upload_put(const void *data, size_t len)
{
    esp_err_t err = esp_ota_write(ctx.upload, data, len);
    if (err != ESP_OK) {
        esp_ota_abort(ctx.upload);
        fail("upload: %s", esp_err_to_name(err));
        return err;
    }
    ctx.upload_done += len;
    set_progress(ctx.upload_done, ctx.upload_size);
    return ESP_OK;
}

esp_err_t ota_upload_write(const void *data, size_t len)
{
    const uint8_t *p = data;
    /* The start is held back until it can be told whose image this is. */
    if (ctx.head_len < HEAD_SIZE) {
        size_t take = HEAD_SIZE - ctx.head_len;
        if (take > len) {
            take = len;
        }
        memcpy(ctx.head + ctx.head_len, p, take);
        ctx.head_len += take;
        p += take;
        len -= take;
        if (ctx.head_len < HEAD_SIZE) {
            return ESP_OK;
        }
        esp_app_desc_t desc;
        memcpy(&desc, ctx.head + DESC_OFFSET, sizeof desc);
        if (ctx.head[0] != ESP_IMAGE_HEADER_MAGIC || !desc_is_ours(&desc)) {
            esp_ota_abort(ctx.upload);
            fail("not an Efflorescence image");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_LOGI(TAG, "upload carries version %s", desc.version);
        esp_err_t err = upload_put(ctx.head, HEAD_SIZE);
        if (err != ESP_OK) {
            return err;
        }
    }
    return len > 0 ? upload_put(p, len) : ESP_OK;
}

esp_err_t ota_upload_end(void)
{
    if (ctx.upload_done != ctx.upload_size) {
        esp_ota_abort(ctx.upload);
        fail("upload cut short at %u of %u bytes", (unsigned)ctx.upload_done,
             (unsigned)ctx.upload_size);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_ota_end(ctx.upload);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(ctx.upload_part);
    }
    if (err != ESP_OK) {
        fail("image rejected: %s", esp_err_to_name(err));
        return err;
    }
    set_phase(OTA_RESTARTING);
    ESP_LOGW(TAG, "uploaded image accepted");
    return ESP_OK;
}

void ota_upload_abort(void)
{
    esp_ota_abort(ctx.upload);
    fail("upload interrupted");
}

void ota_get_status(ota_status_t *out)
{
    memset(out, 0, sizeof *out);
    strlcpy(out->current, current_version(), sizeof out->current);
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run != NULL) {
        strlcpy(out->slot, run->label, sizeof out->slot);
    }
    out->progress = -1;
    if (ctx.lock == NULL) {
        return;
    }
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    out->phase = ctx.phase;
    out->progress = ctx.progress;
    strlcpy(out->available, ctx.manifest.version, sizeof out->available);
    out->newer = ctx.newer;
    strlcpy(out->error, ctx.error, sizeof out->error);
    out->pending_verify = ctx.pending_verify;
    xSemaphoreGive(ctx.lock);
}

static void health_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEALTH_POLL_MS));
        wifi_mgr_status_t w;
        wifi_mgr_get_status(&w);
        ota_health_in_t in = {
            .wifi_configured = ctx.cfg->wifi_ssid[0] != '\0',
            .sta_connected = w.sta_connected,
            .ap_up = w.ap_up,
            .web_up = web_server_up(),
            .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000),
        };
#ifdef APP_OTA_TEST_NO_CONFIRM
        in.web_up = false;
#endif
        ota_health_t verdict = ota_health(&in);
        if (verdict == OTA_HEALTH_ROLLBACK) {
            ESP_LOGE(TAG, "no network after %" PRIu32 " s on the new image: "
                     "rolling back", in.uptime_s);
            esp_ota_mark_app_invalid_rollback_and_reboot();
            /* Only reached when there is nothing to roll back to. */
            ESP_LOGE(TAG, "rollback failed, keeping this image");
            verdict = OTA_HEALTH_CONFIRM;
        }
        if (verdict == OTA_HEALTH_CONFIRM) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "image confirmed after %" PRIu32 " s: %s",
                     in.uptime_s, esp_err_to_name(err));
            xSemaphoreTake(ctx.lock, portMAX_DELAY);
            ctx.pending_verify = false;
            xSemaphoreGive(ctx.lock);
            vTaskDelete(NULL);
        }
    }
}

void ota_start(const appconf_t *cfg)
{
    ctx.cfg = cfg;
    ctx.lock = xSemaphoreCreateMutex();
    ctx.queue = xQueueCreate(1, sizeof(cmd_t));
    configASSERT(ctx.lock != NULL && ctx.queue != NULL);
    ctx.progress = -1;

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        ctx.pending_verify = true;
    }
    ESP_LOGI(TAG, "version %s in %s%s, manifest %s", current_version(),
             run != NULL ? run->label : "?",
             ctx.pending_verify ? ", on probation" : "", CONFIG_APP_UPDATE_URL);

    BaseType_t ok = xTaskCreate(ota_task, "ota", 8192, NULL, 4, NULL);
    configASSERT(ok == pdPASS);
    if (ctx.pending_verify) {
        ok = xTaskCreate(health_task, "ota-health", 3072, NULL, 4, NULL);
        configASSERT(ok == pdPASS);
    }
}
