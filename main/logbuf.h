/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGBUF_SIZE 8192

/* From here on every log line also goes into a ring in RAM: the lamp is
 * sealed and its console cannot be reached. */
void logbuf_start(void);

/* The newest whole lines that fit; returns the byte count. */
size_t logbuf_read(char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
