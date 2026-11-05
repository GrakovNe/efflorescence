/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_log.h"
#include "esp_system.h"

#include "lamp.h"
#include "reset.h"
#include "settings.h"
#include "touch.h"

static const char *TAG = "main";

#ifndef APP_VER
#define APP_VER "dev"
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Efflorescence firmware %s (idf %s, built %s %s)",
             APP_VER, esp_get_idf_version(), __DATE__, __TIME__);

    reset_early_init();
    settings_nvs_init();

    lamp_start();
    touch_start();
}
