/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "log_ring.h"

#include <string.h>

void log_ring_init(log_ring_t *r, char *buf, size_t cap)
{
    r->buf = buf;
    r->cap = cap;
    r->head = 0;
    r->len = 0;
}

void log_ring_write(log_ring_t *r, const char *data, size_t n)
{
    if (r->cap == 0) {
        return;
    }
    if (n >= r->cap) {
        data += n - r->cap;
        n = r->cap;
    }
    size_t tail = r->cap - r->head;
    size_t first = n < tail ? n : tail;
    memcpy(r->buf + r->head, data, first);
    memcpy(r->buf, data + first, n - first);
    r->head = (r->head + n) % r->cap;
    r->len = r->len + n > r->cap ? r->cap : r->len + n;
}

static char at(const log_ring_t *r, size_t i)
{
    size_t start = (r->head + r->cap - r->len) % r->cap;
    return r->buf[(start + i) % r->cap];
}

size_t log_ring_read(const log_ring_t *r, char *out, size_t out_cap)
{
    size_t skip = r->len > out_cap ? r->len - out_cap : 0;
    /* Something was lost before the first byte kept: drop the torn line. */
    if (skip > 0 || r->len == r->cap) {
        size_t i = skip;
        while (i < r->len && at(r, i) != '\n') {
            i++;
        }
        if (i + 1 < r->len) {
            skip = i + 1;
        }
    }
    size_t n = r->len - skip;
    for (size_t i = 0; i < n; i++) {
        out[i] = at(r, skip + i);
    }
    return n;
}
