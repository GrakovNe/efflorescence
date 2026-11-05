/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The last `cap` bytes written, oldest first. */
typedef struct {
    char  *buf;
    size_t cap;
    size_t head; /* where the next byte goes */
    size_t len;
} log_ring_t;

void log_ring_init(log_ring_t *r, char *buf, size_t cap);

void log_ring_write(log_ring_t *r, const char *data, size_t n);

/* Copies up to out_cap bytes, the newest ones if not all fit, and returns
 * how many. A cut never starts in the middle of a line when a line start
 * is available. */
size_t log_ring_read(const log_ring_t *r, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
