/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "logbuf.h"

#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "log_ring.h"

#define LOG_LINE_MAX 256

static char s_store[LOGBUF_SIZE];
static log_ring_t s_ring;
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_console;

/* static: the hook runs on whatever task logs, some with small stacks. */
static char s_line[LOG_LINE_MAX];

static int hook(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int n = s_console(fmt, ap);

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        int len = vsnprintf(s_line, sizeof s_line, fmt, copy);
        if (len > 0) {
            log_ring_write(&s_ring, s_line,
                           len < (int)sizeof s_line ? (size_t)len
                                                    : sizeof s_line - 1);
        }
        xSemaphoreGive(s_lock);
    }
    va_end(copy);
    return n;
}

void logbuf_start(void)
{
    log_ring_init(&s_ring, s_store, sizeof s_store);
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    s_console = esp_log_set_vprintf(hook);
}

size_t logbuf_read(char *out, size_t out_cap)
{
    if (s_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = log_ring_read(&s_ring, out, out_cap);
    xSemaphoreGive(s_lock);
    return n;
}
