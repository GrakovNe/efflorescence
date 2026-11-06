/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "touch.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "gesture.h"
#include "lamp.h"
#include "mqtt_mgr.h"
#include "reset.h"
#include "wifi_mgr.h"

static const char *TAG = "touch";

#define SAMPLE_MS 10
#define SETTLE_MS 600 /* the TTP223 calibrates itself after power-up */

#define POWER_OFF_MS 150

static TaskHandle_t s_task;
static volatile bool s_trace;
static volatile bool s_reset;
static volatile bool s_settled;
static unsigned s_stuck;
static unsigned s_recal;

/* The LED duty the sensor last calibrated itself at. Touch task only. */
#define CAL_UNKNOWN UINT32_MAX
static uint32_t s_cal_duty = CAL_UNKNOWN;

static void IRAM_ATTR touch_isr(void *arg)
{
    (void)arg;
    /* Level-triggered, so it keeps firing while the sensor is high: off
     * until the task has seen the gesture through. */
    gpio_intr_disable(CONFIG_APP_TOUCH_IO_GPIO);
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void power_cycle(void);

static void dispatch(gesture_event_t ev, uint8_t taps)
{
    switch (ev) {
    case GESTURE_TAP:
        ESP_LOGI(TAG, "tap %u", (unsigned)taps);
        lamp_toggle();
        break;
    case GESTURE_SERIES_END:
        ESP_LOGI(TAG, "series of %u", (unsigned)taps);
        mqtt_mgr_taps(taps);
        break;
    case GESTURE_HOLD_BEGIN:
        ESP_LOGI(TAG, "hold");
        lamp_hold_begin();
        wifi_mgr_wake();
        break;
    case GESTURE_HOLD_END:
        ESP_LOGI(TAG, "hold released");
        lamp_hold_end();
        break;
    case GESTURE_RESET_ARMED:
        ESP_LOGW(TAG, "factory reset armed: keep holding");
        lamp_signal(LAMP_SIGNAL_RESET_ARMED);
        break;
    case GESTURE_RESET_READY:
        ESP_LOGW(TAG, "factory reset ready: release to reset");
        lamp_signal(LAMP_SIGNAL_RESET_READY);
        break;
    case GESTURE_RESET_CANCEL:
        ESP_LOGW(TAG, "factory reset cancelled");
        lamp_signal(LAMP_SIGNAL_NONE);
        break;
    case GESTURE_RESET_FIRE:
        ESP_LOGW(TAG, "factory reset, restarting");
        lamp_signal(LAMP_SIGNAL_NONE);
        reset_request_factory();
        break;
    case GESTURE_STUCK:
        /* The output stays high with no finger on it; left alone the
         * sensor takes ten seconds and more to notice. Cutting its power
         * makes it calibrate again at once. */
        s_stuck++;
        ESP_LOGW(TAG, "sensor stuck high (%u since boot): hold undone, "
                 "its power cycled", s_stuck);
        lamp_hold_cancel();
        power_cycle();
        /* That was in the middle of the ramp: once more when the light
         * is back where it was. */
        s_cal_duty = CAL_UNKNOWN;
        break;
    case GESTURE_NONE:
        break;
    }
}

/* What the sensor output itself does, for the bench: the gestures above
 * are what the firmware made of it. */
typedef struct {
    bool    level;
    int64_t since_ms;
    int64_t said_ms;
} trace_t;

static void trace(trace_t *t, bool level, bool woke, int64_t now_ms)
{
    if (level != t->level) {
        if (s_trace) {
            ESP_LOGI(TAG, "raw %d after %lld ms of %d%s", level,
                     (long long)(now_ms - t->since_ms), t->level,
                     woke ? ", woke the task" : "");
        }
        t->level = level;
        t->since_ms = now_ms;
        t->said_ms = now_ms;
    } else if (s_trace && woke) {
        ESP_LOGI(TAG, "woke with raw still %d", level);
    } else if (s_trace && level && now_ms - t->said_ms >= 1000) {
        ESP_LOGI(TAG, "raw still 1, %lld ms now", (long long)(now_ms - t->since_ms));
        t->said_ms = now_ms;
    }
}

static void power_cycle(void)
{
    if (CONFIG_APP_TOUCH_VCC_GPIO < 0) {
        return;
    }
    gpio_set_level(CONFIG_APP_TOUCH_VCC_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(POWER_OFF_MS));
    gpio_set_level(CONFIG_APP_TOUCH_VCC_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    if (gpio_get_level(CONFIG_APP_TOUCH_IO_GPIO) == 1) {
        ESP_LOGW(TAG, "sensor output high right after its power-up");
    }
}

/* The LED's current moves the supply, and the sensor takes a supply that
 * has moved for a touch: for 16 s after a touch it does not calibrate
 * (datasheet), and its output then stays high for good. So once the
 * light is at rest at a new level the sensor gets a fresh start there.
 * It is deaf for POWER_OFF_MS + SETTLE_MS while that happens. */
static void recalibrate(void)
{
    lamp_status_t st;
    lamp_get(&st);
    if (st.busy || st.duty == s_cal_duty) {
        return;
    }
    power_cycle();
    s_recal++;
    ESP_LOGI(TAG, "light at rest at duty %u: sensor calibrated again "
             "(%u since boot)", (unsigned)st.duty, s_recal);
    s_cal_duty = st.duty;
}

static void on_lamp_settled(void)
{
    s_settled = true;
    xTaskNotifyGive(s_task);
}

static void touch_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));

    gesture_t g;
    gesture_init(&g);
    trace_t tr = { .since_ms = esp_timer_get_time() / 1000 };
    /* The sensor has just calibrated itself at whatever the light was
     * doing; if that was a fade, the end of it brings another go. */
    lamp_status_t st;
    lamp_get(&st);
    s_cal_duty = st.busy ? CAL_UNKNOWN : st.duty;
    for (;;) {
        /* Sampling runs only while a gesture is in flight; otherwise the
         * task sleeps until the sensor output moves. */
        bool woke = false;
        if (gesture_active(&g)) {
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
        } else {
            /* Never in the middle of a gesture: a series of taps would
             * lose the ones that fall into the deaf time. Cleared before
             * the lamp is asked, so a fade that ends meanwhile is not
             * missed. */
            if (s_settled) {
                s_settled = false;
                recalibrate();
            }
            gpio_intr_enable(CONFIG_APP_TOUCH_IO_GPIO);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            woke = true;
        }
        if (s_reset) {
            s_reset = false;
            if (g.phase == GESTURE_PHASE_HOLD) {
                lamp_hold_end();
            }
            lamp_signal(LAMP_SIGNAL_NONE);
            ESP_LOGW(TAG, "sensor power cycled on request");
            power_cycle();
            s_cal_duty = CAL_UNKNOWN;
            gesture_init(&g);
            woke = false;
        }
        bool touched = gpio_get_level(CONFIG_APP_TOUCH_IO_GPIO) == 1;
        int64_t now_ms = esp_timer_get_time() / 1000;
        trace(&tr, touched, woke, now_ms);
        gesture_event_t ev = gesture_feed(&g, touched, now_ms);
        dispatch(ev, g.taps);
    }
}

void touch_set_trace(bool on)
{
    s_trace = on;
    ESP_LOGW(TAG, "raw trace %s", on ? "on" : "off");
}

bool touch_trace(void)
{
    return s_trace;
}

unsigned touch_stuck_count(void)
{
    return s_stuck;
}

unsigned touch_recal_count(void)
{
    return s_recal;
}

void touch_reset_sensor(void)
{
    s_reset = true;
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

static void power_pins(void)
{
    int pins[2] = { CONFIG_APP_TOUCH_GND_GPIO, CONFIG_APP_TOUCH_VCC_GPIO };
    int levels[2] = { 0, 1 };
    for (int i = 0; i < 2; i++) {
        if (pins[i] < 0) {
            continue;
        }
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        ESP_ERROR_CHECK(gpio_set_level(pins[i], levels[i]));
        /* These two pins are the sensor's supply. Its LED draws through
         * them whenever it is touched, and the datasheet wants a supply
         * that does not move: the strongest drive moves the least. */
        ESP_ERROR_CHECK(gpio_set_drive_capability(pins[i], GPIO_DRIVE_CAP_3));
        /* Light sleep would otherwise float the pin and cut the sensor's
         * power. */
        ESP_ERROR_CHECK(gpio_sleep_sel_dis(pins[i]));
    }
}

void touch_start(void)
{
    power_pins();

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_APP_TOUCH_IO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        /* Edges are not seen in light sleep; a level is, and it is also
         * what wakes the chip. */
        .intr_type = GPIO_INTR_HIGH_LEVEL,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(CONFIG_APP_TOUCH_IO_GPIO));
    ESP_ERROR_CHECK(gpio_wakeup_enable(CONFIG_APP_TOUCH_IO_GPIO,
                                       GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    gpio_intr_disable(CONFIG_APP_TOUCH_IO_GPIO);

    BaseType_t ok = xTaskCreate(touch_task, "touch", 3072, NULL, 7, &s_task);
    configASSERT(ok == pdPASS);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_APP_TOUCH_IO_GPIO, touch_isr,
                                         NULL));
    lamp_set_settle_listener(on_lamp_settled);

    ESP_LOGI(TAG, "sensor on gpio %d, power: gnd gpio %d, vcc gpio %d",
             CONFIG_APP_TOUCH_IO_GPIO, CONFIG_APP_TOUCH_GND_GPIO,
             CONFIG_APP_TOUCH_VCC_GPIO);
}
