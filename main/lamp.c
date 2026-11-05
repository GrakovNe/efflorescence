/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "lamp.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "dimmer.h"
#include "settings.h"

static const char *TAG = "lamp";

#define TICK_MS       10
#define SAVE_DELAY_MS 5000

#define PWM_MODE    LEDC_LOW_SPEED_MODE
#define PWM_TIMER   LEDC_TIMER_0
#define PWM_CHANNEL LEDC_CHANNEL_0
#define DUTY_MAX    ((1u << CONFIG_APP_PWM_BITS) - 1)
#define DUTY_MIN    (DUTY_MAX * CONFIG_APP_LED_DUTY_MIN_PERMILLE / 1000)

static struct {
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    lamp_core_t core;
    bool soft;
    uint32_t duty;
    uint32_t saved_seq;
    uint32_t seen_seq;
    int64_t changed_ms;
} ctx;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t soft_ms(int32_t transition_ms)
{
    if (transition_ms >= 0) {
        return (uint32_t)transition_ms;
    }
    return ctx.soft ? LAMP_SOFT_MS : 0;
}

static void write_duty(uint32_t duty)
{
    if (duty == ctx.duty) {
        return;
    }
    ctx.duty = duty;
    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
}

static void lamp_task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t now = now_ms();

        xSemaphoreTake(ctx.lock, portMAX_DELAY);
        lamp_core_tick(&ctx.core, now);
        uint16_t level = lamp_core_output(&ctx.core, now);
        bool busy = lamp_core_busy(&ctx.core);
        uint32_t seq = ctx.core.seq;
        bool on = ctx.core.on;
        uint8_t brightness = ctx.core.brightness;
        xSemaphoreGive(ctx.lock);

        write_duty(dimmer_duty(level, DUTY_MIN, DUTY_MAX));

        /* Flash is written once the state has been quiet for a while, not
         * on every step of a slider or a held finger. */
        TickType_t wait = busy ? pdMS_TO_TICKS(TICK_MS) : portMAX_DELAY;
        if (seq != ctx.saved_seq) {
            if (seq != ctx.seen_seq) {
                ctx.seen_seq = seq;
                ctx.changed_ms = now;
            }
            int64_t left_ms = ctx.changed_ms + SAVE_DELAY_MS - now;
            if (left_ms <= 0) {
                if (settings_save_state(on, brightness) == ESP_OK) {
                    ESP_LOGI(TAG, "saved: %s, %u%%", on ? "on" : "off",
                             (unsigned)brightness);
                }
                ctx.saved_seq = seq;
            } else if (!busy) {
                wait = pdMS_TO_TICKS(left_ms);
            }
        }
        ulTaskNotifyTake(pdTRUE, wait);
    }
}

static void wake(void)
{
    xTaskNotifyGive(ctx.task);
}

void lamp_set(bool on, uint8_t brightness, int32_t transition_ms)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_set(&ctx.core, now_ms(), on, brightness, soft_ms(transition_ms));
    xSemaphoreGive(ctx.lock);
    wake();
}

void lamp_toggle(void)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_toggle(&ctx.core, now_ms(), soft_ms(LAMP_TRANSITION_DEFAULT));
    xSemaphoreGive(ctx.lock);
    wake();
}

void lamp_hold_begin(void)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_hold_begin(&ctx.core, now_ms());
    xSemaphoreGive(ctx.lock);
    wake();
}

void lamp_hold_end(void)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_hold_end(&ctx.core, now_ms());
    xSemaphoreGive(ctx.lock);
    wake();
}

void lamp_signal(lamp_signal_t signal)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_signal(&ctx.core, now_ms(), signal);
    xSemaphoreGive(ctx.lock);
    wake();
}

void lamp_get(lamp_status_t *out)
{
    memset(out, 0, sizeof *out);
    if (ctx.lock == NULL) {
        return;
    }
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    out->on = ctx.core.on;
    out->brightness = ctx.core.brightness;
    out->level = ctx.core.level;
    out->seq = ctx.core.seq;
    xSemaphoreGive(ctx.lock);
}

void lamp_start(void)
{
    ctx.lock = xSemaphoreCreateMutex();
    configASSERT(ctx.lock != NULL);
    ctx.soft = true;

    bool on = true;
    uint8_t brightness = LAMP_PCT_DEFAULT;
    settings_load_state(&on, &brightness);
    lamp_core_init(&ctx.core, on, brightness);
    ctx.saved_seq = ctx.core.seq;
    ctx.seen_seq = ctx.core.seq;

    if (CONFIG_APP_LED_GND_GPIO >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_APP_LED_GND_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        ESP_ERROR_CHECK(gpio_set_level(CONFIG_APP_LED_GND_GPIO, 0));
    }

    ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = CONFIG_APP_PWM_BITS,
        .timer_num = PWM_TIMER,
        .freq_hz = CONFIG_APP_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num = CONFIG_APP_LED_PWM_GPIO,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    ESP_LOGI(TAG, "pwm gpio %d, %d Hz, %d bit, duty %" PRIu32 "..%" PRIu32
             "; state: %s, %u%%",
             CONFIG_APP_LED_PWM_GPIO, CONFIG_APP_PWM_FREQ_HZ,
             CONFIG_APP_PWM_BITS, (uint32_t)DUTY_MIN, (uint32_t)DUTY_MAX,
             on ? "on" : "off", (unsigned)ctx.core.brightness);

    BaseType_t ok = xTaskCreate(lamp_task, "lamp", 3072, NULL, 6, &ctx.task);
    configASSERT(ok == pdPASS);
}
