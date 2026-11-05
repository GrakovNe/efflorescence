/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "mqtt_mgr.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "ha_discovery.h"
#include "lamp.h"
#include "light_cmd.h"
#include "ota.h"
#include "time_mgr.h"
#include "wifi_mgr.h"

static const char *TAG = "mqtt";

#define HA_STATUS_TOPIC "homeassistant/status"

#define AVAIL_ONLINE  "online"
#define AVAIL_OFFLINE "offline"
#define INSTALL_WORD  "install"

/* A held finger changes the brightness a hundred times a second. */
#define STATE_GAP_MS 250
#define IDLE_WAIT_MS 5000

typedef enum {
    MSG_WAKE,
    MSG_COMMAND,
    MSG_TAPS,
    MSG_INSTALL,
} msg_kind_t;

typedef struct {
    msg_kind_t kind;
    light_cmd_t cmd;
    uint8_t taps;
} msg_t;

static struct {
    esp_mqtt_client_handle_t client;
    const appconf_t *cfg;
    QueueHandle_t queue;
    bool enabled;
    ha_dev_t dev;
    char dev_id[5];

    char t_avail[APPCONF_TOPIC_MAX + 1];
    char t_state[APPCONF_TOPIC_MAX + 1];
    char t_set[APPCONF_TOPIC_MAX + 1];
    char t_get[APPCONF_TOPIC_MAX + 1];
    char t_event[APPCONF_TOPIC_MAX + 1];
    char t_update[APPCONF_TOPIC_MAX + 1];
    char t_install[APPCONF_TOPIC_MAX + 1];

    /* Set by the event task, acted on by the publish task. */
    volatile bool connected;
    volatile bool need_avail;
    volatile bool need_subscribe;
    volatile bool need_discovery;
    volatile bool need_state;
    volatile bool need_update;

    bool cleanup_done;
    uint32_t state_seq;
    bool state_soft;
    bool state_sent;
    int64_t state_at_ms;
    char update_sent[160];
    int64_t last_pub_epoch;
    uint32_t errors;
} ctx;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool publish(const char *topic, const char *payload, bool retain)
{
    if (esp_mqtt_client_publish(ctx.client, topic, payload, 0, 1, retain) < 0) {
        ctx.errors++;
        ESP_LOGW(TAG, "publish to %s failed", topic);
        return false;
    }
    ctx.last_pub_epoch = time_mgr_unix();
    return true;
}

static void publish_discovery(void)
{
    bool ok = true;
    for (int e = 0; e < HA_ENTITY_COUNT; e++) {
        char topic[HA_TOPIC_MAX];
        ha_config_topic(&ctx.dev, (ha_entity_t)e, topic, sizeof topic);
        /* An empty retained config removes the entity. */
        char *json = ctx.cfg->mqtt_discovery ? ha_config_json(&ctx.dev, (ha_entity_t)e)
                                             : NULL;
        if (ctx.cfg->mqtt_discovery && json == NULL) {
            ok = false;
        } else if (!publish(topic, json != NULL ? json : "", true)) {
            ok = false;
        }
        free(json);
    }
    if (ok) {
        ctx.need_discovery = false;
        ctx.cleanup_done = true;
        ESP_LOGI(TAG, "discovery configs %s",
                 ctx.cfg->mqtt_discovery ? "published" : "removed");
    }
}

static void subscribe_all(void)
{
    const char *topics[] = { ctx.t_set, ctx.t_get, ctx.t_install, HA_STATUS_TOPIC };
    size_t n = sizeof topics / sizeof topics[0] - (ctx.cfg->mqtt_discovery ? 0 : 1);
    for (size_t i = 0; i < n; i++) {
        if (esp_mqtt_client_subscribe(ctx.client, topics[i], 1) < 0) {
            ctx.errors++;
            ESP_LOGW(TAG, "subscribe %s failed", topics[i]);
            return;
        }
    }
    ctx.need_subscribe = false;
    ESP_LOGI(TAG, "subscribed to %s and %u more", ctx.t_set, (unsigned)n - 1);
}

static void publish_state(void)
{
    lamp_status_t l;
    lamp_get(&l);
    cJSON *o = light_state_json(l.on, l.brightness, l.soft);
    char *json = o != NULL ? cJSON_PrintUnformatted(o) : NULL;
    cJSON_Delete(o);
    if (json != NULL && publish(ctx.t_state, json, true)) {
        ctx.need_state = false;
        ctx.state_seq = l.seq;
        ctx.state_soft = l.soft;
        ctx.state_sent = true;
        ctx.state_at_ms = now_ms();
    }
    free(json);
}

static void publish_update(void)
{
    ota_status_t s;
    ota_get_status(&s);
    bool busy = s.phase == OTA_DOWNLOADING || s.phase == OTA_UPLOADING ||
                s.phase == OTA_RESTARTING;
    char *json = ha_update_json(s.current, s.newer ? s.available : NULL, busy,
                                busy ? s.progress : -1);
    if (json == NULL) {
        return;
    }
    if (ctx.need_update || strncmp(json, ctx.update_sent, sizeof ctx.update_sent - 1) != 0) {
        if (publish(ctx.t_update, json, true)) {
            strlcpy(ctx.update_sent, json, sizeof ctx.update_sent);
            ctx.need_update = false;
        }
    }
    free(json);
}

static void handle(const msg_t *m)
{
    switch (m->kind) {
    case MSG_COMMAND:
        lamp_apply(&m->cmd);
        ctx.need_state = true;
        break;
    case MSG_TAPS: {
        char *json = ha_taps_json(m->taps);
        if (json != NULL && ctx.connected) {
            publish(ctx.t_event, json, false);
        }
        free(json);
        break;
    }
    case MSG_INSTALL:
        if (!ota_install()) {
            ESP_LOGW(TAG, "install asked, but there is nothing to install");
        }
        break;
    case MSG_WAKE:
        break;
    }
}

/* Every publish and subscribe happens here. esp-mqtt callbacks must not
 * block, and esp_mqtt_client_* calls from its own event task deadlock it. */
static void publish_task(void *arg)
{
    (void)arg;
    for (;;) {
        msg_t m;
        if (xQueueReceive(ctx.queue, &m, pdMS_TO_TICKS(IDLE_WAIT_MS)) == pdTRUE) {
            handle(&m);
        }
        if (!ctx.connected) {
            continue;
        }
        if (ctx.need_avail && publish(ctx.t_avail, AVAIL_ONLINE, true)) {
            ctx.need_avail = false;
        }
        if (ctx.need_subscribe) {
            subscribe_all();
        }
        if (ctx.need_discovery && (ctx.cfg->mqtt_discovery || !ctx.cleanup_done)) {
            publish_discovery();
        }

        lamp_status_t l;
        lamp_get(&l);
        if (ctx.need_state || !ctx.state_sent || l.seq != ctx.state_seq ||
            l.soft != ctx.state_soft) {
            int64_t wait = ctx.state_at_ms + STATE_GAP_MS - now_ms();
            if (ctx.need_state || wait <= 0) {
                publish_state();
            } else {
                /* Come back for the latest value once the gap is over. */
                vTaskDelay(pdMS_TO_TICKS(wait));
                publish_state();
            }
        }
        publish_update();
    }
}

static bool topic_is(const esp_mqtt_event_t *ev, const char *topic)
{
    return ev->topic_len == (int)strlen(topic) &&
           strncmp(ev->topic, topic, ev->topic_len) == 0;
}

static bool data_is(const esp_mqtt_event_t *ev, const char *word)
{
    return ev->data_len == (int)strlen(word) &&
           strncmp(ev->data, word, ev->data_len) == 0;
}

static void post(const msg_t *m)
{
    if (xQueueSend(ctx.queue, m, 0) != pdTRUE) {
        ctx.errors++;
        ESP_LOGW(TAG, "queue full, message %d dropped", m->kind);
    }
}

static void on_data(const esp_mqtt_event_t *ev)
{
    /* A message split over several events: nothing here is that long. */
    if (ev->current_data_offset != 0 || ev->data_len != ev->total_data_len) {
        return;
    }
    msg_t m = { .kind = MSG_WAKE };
    if (topic_is(ev, ctx.t_set)) {
        char err[96];
        if (!light_cmd_parse_text(ev->data, (size_t)ev->data_len, &m.cmd, err,
                                  sizeof err)) {
            ESP_LOGW(TAG, "bad command: %s", err);
            return;
        }
        m.kind = MSG_COMMAND;
    } else if (topic_is(ev, ctx.t_get)) {
        ctx.need_state = true;
    } else if (topic_is(ev, ctx.t_install)) {
        if (!data_is(ev, INSTALL_WORD)) {
            return;
        }
        m.kind = MSG_INSTALL;
    } else if (topic_is(ev, HA_STATUS_TOPIC)) {
        /* Home Assistant says "online" when it restarts and has forgotten
         * what it discovered. */
        if (!data_is(ev, AVAIL_ONLINE)) {
            return;
        }
        ESP_LOGI(TAG, "home assistant online, republishing discovery");
        ctx.need_discovery = true;
        ctx.need_state = true;
        ctx.need_update = true;
    } else {
        return;
    }
    post(&m);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id,
                       void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_t *ev = data;
    msg_t wake = { .kind = MSG_WAKE };

    switch (id) {
    case MQTT_EVENT_CONNECTED:
        ctx.need_avail = true;
        ctx.need_subscribe = true;
        ctx.need_discovery = true;
        ctx.need_state = true;
        ctx.need_update = true;
        ctx.connected = true;
        ESP_LOGI(TAG, "connected to broker");
        post(&wake);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ctx.connected = false;
        ESP_LOGW(TAG, "disconnected from broker");
        break;

    case MQTT_EVENT_DATA:
        on_data(ev);
        break;

    case MQTT_EVENT_ERROR: {
        /* The client starts before Wi-Fi is up; failing to reach the
         * broker without a network is not an error worth counting. */
        wifi_mgr_status_t w;
        wifi_mgr_get_status(&w);
        if (w.sta_connected) {
            ctx.errors++;
            ESP_LOGW(TAG, "mqtt error (total %" PRIu32 ")", ctx.errors);
        }
        break;
    }

    default:
        break;
    }
}

/* Runs on the lamp task: hand the news over and return. */
static void on_lamp_change(void)
{
    /* A held finger must not crowd commands out of the queue. */
    if (uxQueueMessagesWaiting(ctx.queue) == 0) {
        msg_t wake = { .kind = MSG_WAKE };
        xQueueSend(ctx.queue, &wake, 0);
    }
}

void mqtt_mgr_taps(uint8_t taps)
{
    if (!ctx.enabled || taps < 2) {
        return;
    }
    msg_t m = { .kind = MSG_TAPS, .taps = taps };
    post(&m);
}

void mqtt_mgr_start(const appconf_t *cfg, const char device_id[5])
{
    ctx.cfg = cfg;
    if (!cfg->mqtt_enabled) {
        ESP_LOGI(TAG, "mqtt disabled");
        return;
    }

    appconf_topic(cfg, "availability", ctx.t_avail, sizeof ctx.t_avail);
    appconf_topic(cfg, "state", ctx.t_state, sizeof ctx.t_state);
    appconf_topic(cfg, "set", ctx.t_set, sizeof ctx.t_set);
    appconf_topic(cfg, "get", ctx.t_get, sizeof ctx.t_get);
    appconf_topic(cfg, "event", ctx.t_event, sizeof ctx.t_event);
    appconf_topic(cfg, "update", ctx.t_update, sizeof ctx.t_update);
    appconf_topic(cfg, "update/install", ctx.t_install, sizeof ctx.t_install);

    strlcpy(ctx.dev_id, device_id, sizeof ctx.dev_id);
    ctx.dev = (ha_dev_t) {
        .prefix = cfg->mqtt_prefix,
        .device_id = ctx.dev_id,
        .name = cfg->device_name,
        .hostname = cfg->hostname,
        .version = esp_app_get_description()->version,
    };

    ctx.queue = xQueueCreate(8, sizeof(msg_t));
    configASSERT(ctx.queue != NULL);

    esp_mqtt_client_config_t mc = { 0 };
    char uri[APPCONF_HOST_MAX + 32];
    bool ipv6 = strchr(cfg->mqtt_host, ':') != NULL;
    snprintf(uri, sizeof uri, "%s://%s%s%s:%u", cfg->mqtt_tls ? "mqtts" : "mqtt",
             ipv6 ? "[" : "", cfg->mqtt_host, ipv6 ? "]" : "",
             (unsigned)cfg->mqtt_port);
    mc.broker.address.uri = uri;
    if (cfg->mqtt_username[0] != '\0') {
        mc.credentials.username = cfg->mqtt_username;
    }
    if (cfg->mqtt_password[0] != '\0') {
        mc.credentials.authentication.password = cfg->mqtt_password;
    }

    if (cfg->mqtt_tls) {
        if (cfg->mqtt_skip_verify) {
            ESP_LOGW(TAG, "TLS certificate verification is disabled");
        } else if (cfg->mqtt_ca_cert[0] != '\0') {
            mc.broker.verification.certificate = cfg->mqtt_ca_cert;
        } else {
            mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }
    }

    mc.session.last_will.topic = ctx.t_avail;
    mc.session.last_will.msg = AVAIL_OFFLINE;
    mc.session.last_will.qos = 1;
    mc.session.last_will.retain = true;
    mc.session.keepalive = 60;
    mc.network.reconnect_timeout_ms = 5000;
    mc.network.timeout_ms = 10000;

    ctx.client = esp_mqtt_client_init(&mc);
    if (ctx.client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }
    esp_mqtt_client_register_event(ctx.client, ESP_EVENT_ANY_ID, mqtt_event,
                                   NULL);
    esp_err_t err = esp_mqtt_client_start(ctx.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(err));
        return;
    }
    ctx.enabled = true;

    BaseType_t ok = xTaskCreate(publish_task, "mqtt_pub", 6144, NULL, 4, NULL);
    configASSERT(ok == pdPASS);
    lamp_set_listener(on_lamp_change);
    ESP_LOGI(TAG, "mqtt started: %s, prefix %s, discovery %s", uri,
             cfg->mqtt_prefix, cfg->mqtt_discovery ? "on" : "off");
}

void mqtt_mgr_get_status(mqtt_mgr_status_t *out)
{
    out->enabled = ctx.enabled;
    out->connected = ctx.connected;
    out->last_publish = ctx.last_pub_epoch;
    out->errors = ctx.errors;
}
