/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "sched_mgr.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lamp.h"
#include "settings.h"
#include "time_mgr.h"

static const char *TAG = "sched";

#define NO_CLOCK_POLL_MS 5000
/* A tick that comes a little late still runs the minutes it skipped;
 * anything older is a missed event and stays missed. */
#define CATCH_UP_MIN 2

static struct {
    SemaphoreHandle_t lock;
    sched_t sched;
    int64_t last_stamp; /* the last minute handled, minutes since the epoch */
} ctx;

static int wday_of(const struct tm *tm)
{
    return (tm->tm_wday + 6) % 7;
}

static void run_minute(const sched_t *s, int64_t stamp)
{
    time_t t = (time_t)(stamp * 60);
    struct tm tm;
    localtime_r(&t, &tm);
    uint8_t due[SCHED_MAX];
    int n = sched_due(s, wday_of(&tm), tm.tm_hour * 60 + tm.tm_min, due);
    for (int i = 0; i < n; i++) {
        const sched_entry_t *e = &s->entry[due[i]];
        ESP_LOGI(TAG, "entry %d at %02d:%02d: %s, brightness %u, over %u s",
                 due[i] + 1, tm.tm_hour, tm.tm_min, e->on ? "on" : "off",
                 (unsigned)e->brightness, (unsigned)e->transition_s);
        light_cmd_t cmd;
        sched_command(e, &cmd);
        lamp_apply(&cmd);
    }
}

static void sched_task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t now = time_mgr_unix();
        if (now < 0) {
            vTaskDelay(pdMS_TO_TICKS(NO_CLOCK_POLL_MS));
            continue;
        }
        int64_t stamp = now / 60;
        if (stamp != ctx.last_stamp) {
            sched_t s;
            sched_mgr_get(&s);
            int64_t from = stamp;
            if (ctx.last_stamp != 0 && stamp > ctx.last_stamp &&
                stamp - ctx.last_stamp <= CATCH_UP_MIN) {
                from = ctx.last_stamp + 1;
            }
            for (int64_t m = from; m <= stamp; m++) {
                run_minute(&s, m);
            }
            ctx.last_stamp = stamp;
        }
        /* Sleep to just past the next minute boundary. */
        vTaskDelay(pdMS_TO_TICKS((60 - now % 60) * 1000 + 200));
    }
}

void sched_mgr_get(sched_t *out)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    *out = ctx.sched;
    xSemaphoreGive(ctx.lock);
}

esp_err_t sched_mgr_set(const sched_t *s)
{
    esp_err_t err = settings_save_schedule(s);
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    ctx.sched = *s;
    xSemaphoreGive(ctx.lock);
    ESP_LOGI(TAG, "schedule replaced: %u entries", (unsigned)s->count);
    return ESP_OK;
}

void sched_mgr_get_status(sched_mgr_status_t *out)
{
    memset(out, 0, sizeof *out);
    out->next = -1;
    struct tm tm;
    if (!time_mgr_local(&tm)) {
        return;
    }
    out->clock_set = true;
    out->wday = wday_of(&tm);
    out->minute = tm.tm_hour * 60 + tm.tm_min;
    sched_t s;
    sched_mgr_get(&s);
    out->next = sched_next(&s, out->wday, out->minute, &out->next_in_min);
}

void sched_mgr_start(void)
{
    ctx.lock = xSemaphoreCreateMutex();
    configASSERT(ctx.lock != NULL);
    settings_load_schedule(&ctx.sched);
    ESP_LOGI(TAG, "%u entries", (unsigned)ctx.sched.count);

    BaseType_t ok = xTaskCreate(sched_task, "sched", 4096, NULL, 4, NULL);
    configASSERT(ok == pdPASS);
}
