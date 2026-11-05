/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "battery.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "battery_logic.h"
#include "lamp.h"

static const char *TAG = "battery";

#define STORE_MAGIC 0xBA77E21Fu
#define TICK_S      5

/* RTC memory lives through a brownout reset and through deep sleep and
 * is lost with the supply, which is exactly how long this is true. */
typedef struct {
    uint32_t magic;
    uint32_t check;
    uint8_t  low;
    uint8_t  streak;
    uint8_t  fake; /* bench: treat the next start as a brownout */
} store_t;

RTC_NOINIT_ATTR static store_t s_store;

static battery_state_t s_state;
static volatile bool s_low;
static bool s_dark;

static void store_save(void)
{
    s_store.magic = STORE_MAGIC;
    s_store.check = ~STORE_MAGIC;
    s_store.low = s_state.low;
    s_store.streak = s_state.streak;
    s_low = s_state.low;
}

static void drive(int pin, int level)
{
    if (pin < 0) {
        return;
    }
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(pin, level);
}

/* The sensor is powered from two pins that must keep their levels while
 * everything else is off. The LED driver needs no such care: with its
 * input floating the optocoupler passes nothing. */
static void sleep_until_touch(void)
{
    ESP_LOGW(TAG, "brownout %d times in a row: deep sleep until the sensor "
             "is touched", BATTERY_STREAK_LIMIT);
    drive(CONFIG_APP_LED_PWM_GPIO, 0);
    drive(CONFIG_APP_TOUCH_GND_GPIO, 0);
    drive(CONFIG_APP_TOUCH_VCC_GPIO, 1);
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << CONFIG_APP_TOUCH_IO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in);
    if (CONFIG_APP_TOUCH_GND_GPIO >= 0) {
        gpio_hold_en(CONFIG_APP_TOUCH_GND_GPIO);
    }
    if (CONFIG_APP_TOUCH_VCC_GPIO >= 0) {
        gpio_hold_en(CONFIG_APP_TOUCH_VCC_GPIO);
    }
    /* The sensor calibrates after power-up; a finger still on it would
     * wake the lamp at once. */
    vTaskDelay(pdMS_TO_TICKS(700));
    for (int i = 0; i < 50 && gpio_get_level(CONFIG_APP_TOUCH_IO_GPIO) == 1; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_deep_sleep_enable_gpio_wakeup(1ULL << CONFIG_APP_TOUCH_IO_GPIO,
                                      ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_deep_sleep_start();
}

void battery_early_init(void)
{
    /* Holds set before a deep sleep outlive it. */
    if (CONFIG_APP_TOUCH_GND_GPIO >= 0) {
        gpio_hold_dis(CONFIG_APP_TOUCH_GND_GPIO);
    }
    if (CONFIG_APP_TOUCH_VCC_GPIO >= 0) {
        gpio_hold_dis(CONFIG_APP_TOUCH_VCC_GPIO);
    }

    battery_reset_t why;
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  why = BATTERY_RST_POWER_ON; break;
    case ESP_RST_BROWNOUT: why = BATTERY_RST_BROWNOUT; break;
    default:               why = BATTERY_RST_OTHER;    break;
    }
    if (s_store.magic != STORE_MAGIC || s_store.check != ~STORE_MAGIC) {
        memset(&s_store, 0, sizeof s_store);
        why = why == BATTERY_RST_BROWNOUT ? why : BATTERY_RST_POWER_ON;
    }
    if (s_store.fake != 0) {
        s_store.fake = 0;
        why = BATTERY_RST_BROWNOUT;
        ESP_LOGW(TAG, "bench: this start is treated as a brownout");
    }

    s_state.low = s_store.low != 0;
    s_state.streak = s_store.streak;
    battery_boot_t boot = battery_on_boot(&s_state, why);
    store_save();

    if (boot == BATTERY_BOOT_SLEEP) {
        sleep_until_touch();
    }
    s_dark = boot == BATTERY_BOOT_DARK;
    if (s_state.low) {
        ESP_LOGW(TAG, "battery low%s (brownouts in a row: %u)",
                 s_dark ? ", starting dark" : "", (unsigned)s_state.streak);
    }
}

static void battery_task(void *arg)
{
    (void)arg;
    while (battery_watching(&s_state)) {
        vTaskDelay(pdMS_TO_TICKS(TICK_S * 1000));
        lamp_status_t l;
        lamp_get(&l);
        bool led_on = l.on && l.level > 0;
        if (battery_tick(&s_state, led_on,
                         (uint32_t)(esp_timer_get_time() / 1000000), TICK_S)) {
            ESP_LOGI(TAG, "the LED held for %d s: battery no longer low",
                     BATTERY_CLEAR_AFTER_S);
        }
        store_save();
    }
    vTaskDelete(NULL);
}

void battery_start(void)
{
    if (!battery_watching(&s_state)) {
        return;
    }
    BaseType_t ok = xTaskCreate(battery_task, "battery", 3072, NULL, 3, NULL);
    configASSERT(ok == pdPASS);
}

bool battery_low(void)
{
    return s_low;
}

bool battery_boot_dark(void)
{
    return s_dark;
}

bool battery_simulate(const char *what)
{
    if (strcmp(what, "clear") == 0) {
        s_state.low = false;
        s_state.streak = 0;
        store_save();
        return true;
    }
    if (strcmp(what, "brownout") == 0) {
        s_store.fake = 1;
    } else if (strcmp(what, "sleep") == 0) {
        s_state.streak = BATTERY_STREAK_LIMIT - 1;
        store_save();
        s_store.fake = 1;
    } else {
        return false;
    }
    lamp_flush();
    esp_restart();
    return true;
}
