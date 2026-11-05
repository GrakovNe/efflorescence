/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "log_ring.h"

#include "tinytest.h"

static void put(log_ring_t *r, const char *s)
{
    log_ring_write(r, s, strlen(s));
}

static const char *get(const log_ring_t *r, char *out, size_t cap)
{
    size_t n = log_ring_read(r, out, cap - 1);
    out[n] = '\0';
    return out;
}

static void test_plain(void)
{
    char buf[64], out[80];
    log_ring_t r;
    log_ring_init(&r, buf, sizeof buf);
    CHECK_STR(get(&r, out, sizeof out), "");

    put(&r, "one\n");
    put(&r, "two\n");
    CHECK_STR(get(&r, out, sizeof out), "one\ntwo\n");
    CHECK(r.len == 8);
}

static void test_wrap_drops_torn_line(void)
{
    char buf[16], out[32];
    log_ring_t r;
    log_ring_init(&r, buf, sizeof buf);
    put(&r, "alpha\n");
    put(&r, "bravo\n");
    put(&r, "charlie\n");
    /* 20 bytes into 16: "alpha\n" is partly gone. */
    CHECK_STR(get(&r, out, sizeof out), "bravo\ncharlie\n");

    put(&r, "delta\n");
    CHECK_STR(get(&r, out, sizeof out), "charlie\ndelta\n");
    for (int i = 0; i < 100; i++) {
        put(&r, "xy\n");
    }
    CHECK_STR(get(&r, out, sizeof out), "xy\nxy\nxy\nxy\nxy\n");
}

static void test_small_reader(void)
{
    char buf[64], out[16];
    log_ring_t r;
    log_ring_init(&r, buf, sizeof buf);
    put(&r, "first line\n");
    put(&r, "second\n");
    put(&r, "third\n");
    /* 15 bytes of room: the newest whole lines. */
    CHECK_STR(get(&r, out, sizeof out), "second\nthird\n");
}

static void test_oversized_and_unterminated(void)
{
    char buf[8], out[16];
    log_ring_t r;
    log_ring_init(&r, buf, sizeof buf);
    put(&r, "0123456789abcdef");
    /* No line start to cut at: the newest bytes as they are. */
    CHECK_STR(get(&r, out, sizeof out), "89abcdef");

    put(&r, "\nok\n");
    CHECK_STR(get(&r, out, sizeof out), "ok\n");

    log_ring_t none;
    log_ring_init(&none, buf, 0);
    put(&none, "x");
    CHECK(log_ring_read(&none, out, sizeof out) == 0);
}

int main(void)
{
    test_plain();
    test_wrap_drops_torn_line();
    test_small_reader();
    test_oversized_and_unterminated();
    REPORT();
}
