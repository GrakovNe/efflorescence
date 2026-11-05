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

#include "battery.h"
#include "dimmer.h"
#include "power.h"
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
    appconf_t *cfg;
    SemaphoreHandle_t lock;
    TaskHandle_t task;
    lamp_core_t core;

    uint16_t floor;
    uint32_t duty;
    uint32_t freq_hz;
    int32_t raw_duty;      /* < 0: no override */
    uint32_t want_freq_hz;

    void (*volatile changed)(void);
    uint32_t told_seq;
    bool told_soft;

    uint32_t saved_seq;
    uint32_t seen_seq;
    int64_t changed_ms;
} ctx;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t transition(int32_t transition_ms)
{
    if (transition_ms >= 0) {
        return (uint32_t)transition_ms;
    }
    return ctx.cfg->soft_switch ? LAMP_TRANSITION_SOFT : 0;
}

static void write_pwm(uint32_t duty, uint32_t freq_hz)
{
    if (freq_hz != ctx.freq_hz) {
        esp_err_t err = ledc_set_freq(PWM_MODE, PWM_TIMER, freq_hz);
        if (err == ESP_OK) {
            ctx.freq_hz = freq_hz;
            ESP_LOGI(TAG, "pwm %" PRIu32 " Hz", freq_hz);
        } else {
            ESP_LOGW(TAG, "pwm %" PRIu32 " Hz: %s", freq_hz, esp_err_to_name(err));
            ctx.want_freq_hz = ctx.freq_hz;
        }
    }
    if (duty > DUTY_MAX) {
        duty = DUTY_MAX;
    }
    if (duty == ctx.duty) {
        return;
    }
    ctx.duty = duty;
    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
}

/* Call with the lock held. */
static void save_state(void)
{
    if (ctx.core.seq == ctx.saved_seq) {
        return;
    }
    if (settings_save_state(ctx.core.on, ctx.core.brightness) == ESP_OK) {
        ESP_LOGI(TAG, "saved: %s, %u%%", ctx.core.on ? "on" : "off",
                 (unsigned)ctx.core.brightness);
    }
    ctx.saved_seq = ctx.core.seq;
}

static void lamp_task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t now = now_ms();

        xSemaphoreTake(ctx.lock, portMAX_DELAY);
        lamp_core_tick(&ctx.core, now);
        uint32_t duty = ctx.raw_duty >= 0
            ? (uint32_t)ctx.raw_duty
            : dimmer_duty(lamp_core_output(&ctx.core, now), DUTY_MAX);
        bool busy = lamp_core_busy(&ctx.core);
        /* The PWM clock stops in light sleep: stay awake while the LED
         * shows anything, and let go only once it is dark. */
        bool lit = duty != 0 || busy;
        if (lit) {
            power_led_active(true);
        }
        write_pwm(duty, ctx.want_freq_hz);
        if (!lit) {
            power_led_active(false);
        }

        /* Flash is written once the state has been quiet for a while, not
         * on every step of a slider or a held finger. */
        TickType_t wait = busy ? pdMS_TO_TICKS(TICK_MS) : portMAX_DELAY;
        if (ctx.core.seq != ctx.saved_seq) {
            if (ctx.core.seq != ctx.seen_seq) {
                ctx.seen_seq = ctx.core.seq;
                ctx.changed_ms = now;
            }
            int64_t left_ms = ctx.changed_ms + SAVE_DELAY_MS - now;
            if (left_ms <= 0) {
                save_state();
            } else if (!busy) {
                wait = pdMS_TO_TICKS(left_ms);
            }
        }
        uint32_t seq = ctx.core.seq;
        bool soft = ctx.cfg->soft_switch;
        xSemaphoreGive(ctx.lock);

        void (*changed)(void) = ctx.changed;
        if (changed != NULL && (seq != ctx.told_seq || soft != ctx.told_soft)) {
            ctx.told_seq = seq;
            ctx.told_soft = soft;
            changed();
        }

        ulTaskNotifyTake(pdTRUE, wait);
    }
}

static void wake(void)
{
    xTaskNotifyGive(ctx.task);
}

void lamp_apply(const light_cmd_t *cmd)
{
    bool soft_changed = false;
    bool soft = false;

    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    if (cmd->soft >= 0 && ctx.cfg->soft_switch != (cmd->soft != 0)) {
        ctx.cfg->soft_switch = cmd->soft != 0;
        soft = ctx.cfg->soft_switch;
        soft_changed = true;
    }
    if (cmd->state != LIGHT_KEEP || cmd->brightness != 0) {
        bool on = ctx.core.on;
        if (cmd->state == LIGHT_ON) {
            on = true;
        } else if (cmd->state == LIGHT_OFF) {
            on = false;
        } else if (cmd->state == LIGHT_TOGGLE) {
            on = !on;
        }
        lamp_core_set(&ctx.core, now_ms(), on, cmd->brightness,
                      transition(cmd->transition_ms));
    }
    xSemaphoreGive(ctx.lock);

    if (soft_changed) {
        settings_save_soft(soft);
        ESP_LOGI(TAG, "soft on/off: %s", soft ? "on" : "off");
    }
    wake();
}

void lamp_toggle(void)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_toggle(&ctx.core, now_ms(), transition(-1));
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

void lamp_hold_cancel(void)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    lamp_core_hold_cancel(&ctx.core, now_ms());
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
    out->soft = ctx.cfg->soft_switch;
    out->duty = ctx.duty;
    out->duty_min = DUTY_MIN;
    out->duty_max = DUTY_MAX;
    out->freq_hz = ctx.freq_hz;
    out->raw = ctx.raw_duty >= 0;
    xSemaphoreGive(ctx.lock);
}

void lamp_set_listener(void (*changed)(void))
{
    ctx.changed = changed;
}

void lamp_flush(void)
{
    if (ctx.lock == NULL) {
        return;
    }
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    save_state();
    xSemaphoreGive(ctx.lock);
}

void lamp_debug_pwm(int32_t duty, uint32_t freq_hz)
{
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    if (duty < 0) {
        ctx.raw_duty = -1;
        ctx.want_freq_hz = CONFIG_APP_PWM_FREQ_HZ;
    } else {
        ctx.raw_duty = duty;
        if (freq_hz != 0) {
            ctx.want_freq_hz = freq_hz;
        }
    }
    xSemaphoreGive(ctx.lock);
    wake();
}

void lamp_start(appconf_t *cfg)
{
    ctx.cfg = cfg;
    ctx.lock = xSemaphoreCreateMutex();
    configASSERT(ctx.lock != NULL);

    bool on = true;
    uint8_t brightness = LAMP_PCT_DEFAULT;
    settings_load_state(&on, &brightness);
    if (battery_boot_dark()) {
        on = false;
    }
    /* The floor is known as a duty, the lamp thinks in lightness. */
    ctx.floor = dimmer_level_for_duty(DUTY_MIN, DUTY_MAX);
    lamp_core_init(&ctx.core, on, brightness, ctx.floor);
    ctx.saved_seq = ctx.core.seq;
    ctx.seen_seq = ctx.core.seq;
    ctx.told_seq = ctx.core.seq;
    ctx.told_soft = cfg->soft_switch;
    ctx.raw_duty = -1;
    ctx.freq_hz = CONFIG_APP_PWM_FREQ_HZ;
    ctx.want_freq_hz = CONFIG_APP_PWM_FREQ_HZ;

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
        /* Light sleep would otherwise float the pin. */
        ESP_ERROR_CHECK(gpio_sleep_sel_dis(CONFIG_APP_LED_GND_GPIO));
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
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(CONFIG_APP_LED_PWM_GPIO));

    ESP_LOGI(TAG, "pwm gpio %d, %d Hz, %d bit, duty %" PRIu32 "..%" PRIu32
             " (lightness floor %u%%); state: %s, %u%%, soft %s",
             CONFIG_APP_LED_PWM_GPIO, CONFIG_APP_PWM_FREQ_HZ,
             CONFIG_APP_PWM_BITS, (uint32_t)DUTY_MIN, (uint32_t)DUTY_MAX,
             (unsigned)(ctx.floor * 100u / LAMP_LEVEL_MAX),
             on ? "on" : "off", (unsigned)ctx.core.brightness,
             cfg->soft_switch ? "on" : "off");

    BaseType_t ok = xTaskCreate(lamp_task, "lamp", 4096, NULL, 6, &ctx.task);
    configASSERT(ok == pdPASS);
}
