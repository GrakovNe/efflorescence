/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "power.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_pm.h"
#include "sdkconfig.h"

static const char *TAG = "power";

static esp_pm_lock_handle_t s_led_lock;
static bool s_led_active;

void power_start(void)
{
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "led", &s_led_lock));

    /* The low end stays at 80 MHz: below it the APB clock changes and
     * with it everything timed from it. Sleep is what saves the power. */
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 80,
        .light_sleep_enable = true,
    };
    esp_err_t err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure: %s, no sleep", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "light sleep on, cpu %d..%d MHz", pm.min_freq_mhz,
             pm.max_freq_mhz);
}

void power_led_active(bool active)
{
    if (s_led_lock == NULL || active == s_led_active) {
        return;
    }
    s_led_active = active;
    if (active) {
        esp_pm_lock_acquire(s_led_lock);
    } else {
        esp_pm_lock_release(s_led_lock);
    }
}

size_t power_dump(char *out, size_t out_cap)
{
    if (out_cap == 0) {
        return 0;
    }
    out[0] = '\0';
    FILE *f = fmemopen(out, out_cap, "w");
    if (f == NULL) {
        return 0;
    }
    esp_pm_dump_locks(f);
    long n = ftell(f);
    fclose(f);
    return n > 0 ? (size_t)n : 0;
}
