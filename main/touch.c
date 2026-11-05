/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "touch.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "gesture.h"
#include "lamp.h"
#include "reset.h"
#include "wifi_mgr.h"

static const char *TAG = "touch";

#define SAMPLE_MS 10
#define SETTLE_MS 600 /* the TTP223 calibrates itself after power-up */

static TaskHandle_t s_task;

static void IRAM_ATTR touch_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void dispatch(gesture_event_t ev, uint8_t taps)
{
    switch (ev) {
    case GESTURE_TAP:
        ESP_LOGI(TAG, "tap %u", (unsigned)taps);
        lamp_toggle();
        break;
    case GESTURE_SERIES_END:
        ESP_LOGI(TAG, "series of %u", (unsigned)taps);
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
    case GESTURE_NONE:
        break;
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));

    gesture_t g;
    gesture_init(&g);
    for (;;) {
        /* Sampling runs only while a gesture is in flight; otherwise the
         * task sleeps until the sensor output moves. */
        if (gesture_active(&g)) {
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        bool touched = gpio_get_level(CONFIG_APP_TOUCH_IO_GPIO) == 1;
        gesture_event_t ev = gesture_feed(&g, touched,
                                          esp_timer_get_time() / 1000);
        dispatch(ev, g.taps);
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
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    BaseType_t ok = xTaskCreate(touch_task, "touch", 3072, NULL, 7, &s_task);
    configASSERT(ok == pdPASS);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_APP_TOUCH_IO_GPIO, touch_isr,
                                         NULL));

    ESP_LOGI(TAG, "sensor on gpio %d, power: gnd gpio %d, vcc gpio %d",
             CONFIG_APP_TOUCH_IO_GPIO, CONFIG_APP_TOUCH_GND_GPIO,
             CONFIG_APP_TOUCH_VCC_GPIO);
}
