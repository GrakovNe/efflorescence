/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "schedule.h"

#include <string.h>

#include "tinytest.h"

enum { MON, TUE, WED, THU, FRI, SAT, SUN };

#define WEEKDAYS 0x1f
#define WEEKEND  0x60
#define AT(h, m) ((uint16_t)((h) * 60 + (m)))

static sched_entry_t entry(uint8_t days, uint16_t minute, bool on)
{
    sched_entry_t e = { .enabled = true, .days = days, .minute = minute, .on = on };
    return e;
}

static sched_t morning_and_night(void)
{
    sched_t s = { .count = 3 };
    s.entry[0] = entry(WEEKDAYS, AT(7, 0), true);
    s.entry[0].brightness = 60;
    s.entry[0].transition_s = 1800;
    s.entry[1] = entry(SCHED_DAYS_ALL, AT(23, 30), false);
    s.entry[2] = entry(WEEKEND, AT(9, 0), true);
    return s;
}

static void test_validate(void)
{
    char err[96];
    sched_t s = morning_and_night();
    CHECK(sched_validate(&s, err, sizeof err));

    sched_t empty = { 0 };
    CHECK(sched_validate(&empty, err, sizeof err));

    s = morning_and_night();
    s.entry[1].days = 0;
    CHECK(!sched_validate(&s, err, sizeof err));
    CHECK_STR(err, "entry 2: pick at least one day");

    s = morning_and_night();
    s.entry[0].days = 0x80;
    CHECK(!sched_validate(&s, err, sizeof err));

    s = morning_and_night();
    s.entry[2].minute = 1440;
    CHECK(!sched_validate(&s, err, sizeof err));
    CHECK_STR(err, "entry 3: bad time");

    s = morning_and_night();
    s.entry[0].brightness = 101;
    CHECK(!sched_validate(&s, err, sizeof err));
    s = morning_and_night();
    s.entry[1].brightness = 50;
    CHECK(!sched_validate(&s, err, sizeof err));

    s = morning_and_night();
    s.entry[0].transition_s = 7201;
    CHECK(!sched_validate(&s, err, sizeof err));

    s.count = SCHED_MAX + 1;
    CHECK(!sched_validate(&s, err, sizeof err));
}

static void test_due(void)
{
    sched_t s = morning_and_night();
    uint8_t out[SCHED_MAX];

    CHECK(sched_due(&s, MON, AT(7, 0), out) == 1 && out[0] == 0);
    CHECK(sched_due(&s, FRI, AT(7, 0), out) == 1);
    CHECK(sched_due(&s, SAT, AT(7, 0), out) == 0);
    CHECK(sched_due(&s, MON, AT(7, 1), out) == 0);
    CHECK(sched_due(&s, MON, AT(6, 59), out) == 0);
    CHECK(sched_due(&s, SUN, AT(23, 30), out) == 1 && out[0] == 1);
    CHECK(sched_due(&s, SUN, AT(9, 0), out) == 1 && out[0] == 2);

    s.entry[0].enabled = false;
    CHECK(sched_due(&s, MON, AT(7, 0), out) == 0);

    /* Two at the same minute come out in list order: the later one wins. */
    s = morning_and_night();
    s.entry[2] = entry(SCHED_DAYS_ALL, AT(7, 0), false);
    CHECK(sched_due(&s, TUE, AT(7, 0), out) == 2 && out[0] == 0 && out[1] == 2);
}

static void test_next(void)
{
    sched_t s = morning_and_night();
    int in = 0;

    CHECK(sched_next(&s, MON, AT(6, 0), &in) == 0 && in == 60);
    CHECK(sched_next(&s, MON, AT(7, 0), &in) == 1 && in == 16 * 60 + 30);
    CHECK(sched_next(&s, MON, AT(23, 45), &in) == 0 && in == 7 * 60 + 15);
    CHECK(sched_next(&s, FRI, AT(23, 45), &in) == 2 && in == 9 * 60 + 15);
    CHECK(sched_next(&s, SUN, AT(23, 45), &in) == 0 && in == 7 * 60 + 15);

    /* One entry, due right now: its next start is a week away. */
    sched_t one = { .count = 1 };
    one.entry[0] = entry(1u << WED, AT(12, 0), true);
    CHECK(sched_next(&one, WED, AT(12, 0), &in) == 0 && in == SCHED_WEEK_MINUTES);
    CHECK(sched_next(&one, WED, AT(12, 1), &in) == 0 && in == SCHED_WEEK_MINUTES - 1);
    CHECK(sched_next(&one, TUE, AT(12, 0), &in) == 0 && in == SCHED_MINUTES);

    one.entry[0].enabled = false;
    CHECK(sched_next(&one, WED, AT(0, 0), &in) == -1);
    sched_t empty = { 0 };
    CHECK(sched_next(&empty, MON, 0, &in) == -1);
}

static void test_command(void)
{
    sched_t s = morning_and_night();
    light_cmd_t c;

    sched_command(&s.entry[0], &c);
    CHECK(c.state == LIGHT_ON && c.brightness == 60);
    CHECK(c.transition_ms == 1800000 && c.soft == -1);

    sched_command(&s.entry[1], &c);
    CHECK(c.state == LIGHT_OFF && c.brightness == 0 && c.transition_ms == -1);

    /* "On" without a brightness keeps the one the lamp has. */
    sched_command(&s.entry[2], &c);
    CHECK(c.state == LIGHT_ON && c.brightness == 0 && c.transition_ms == -1);
}

static bool parse(const char *json, sched_t *s, char *err, size_t err_sz)
{
    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL);
    err[0] = '\0';
    bool ok = sched_from_json(root, s, err, err_sz);
    cJSON_Delete(root);
    return ok;
}

static void test_json_in(void)
{
    sched_t s;
    char err[96];

    CHECK(parse("{\"entries\":[]}", &s, err, sizeof err) && s.count == 0);

    CHECK(parse("{\"entries\":[{\"days\":[1,2,3,4,5],\"time\":\"07:00\","
                "\"state\":\"ON\",\"brightness\":60,\"transition\":1800},"
                "{\"enabled\":false,\"days\":[7],\"time\":\"23:59\","
                "\"state\":\"OFF\"}]}", &s, err, sizeof err));
    CHECK(s.count == 2);
    CHECK(s.entry[0].enabled && s.entry[0].days == WEEKDAYS);
    CHECK(s.entry[0].minute == AT(7, 0) && s.entry[0].on);
    CHECK(s.entry[0].brightness == 60 && s.entry[0].transition_s == 1800);
    CHECK(!s.entry[1].enabled && s.entry[1].days == (1u << SUN));
    CHECK(s.entry[1].minute == AT(23, 59) && !s.entry[1].on);
    CHECK(s.entry[1].brightness == 0 && s.entry[1].transition_s == 0);

    CHECK(parse("{\"entries\":[{\"days\":[6,6,7],\"time\":\"00:00\","
                "\"state\":\"ON\",\"brightness\":null,\"transition\":null}]}",
                &s, err, sizeof err));
    CHECK(s.entry[0].days == WEEKEND && s.entry[0].brightness == 0);
}

static void test_json_rejects(void)
{
    sched_t s;
    char err[96];

    CHECK(!parse("{}", &s, err, sizeof err));
    CHECK(!parse("{\"entries\":{}}", &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[7]}", &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[],\"time\":\"07:00\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(strstr(err, "at least one day") != NULL);
    CHECK(!parse("{\"entries\":[{\"days\":[0],\"time\":\"07:00\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[8],\"time\":\"07:00\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":\"mon\",\"time\":\"07:00\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"7:00\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(strstr(err, "07:30") != NULL);
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"24:00\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"07:60\",\"state\":\"ON\"}]}",
                 &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"07:00\",\"state\":\"TOGGLE\"}]}",
                 &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"07:00\",\"state\":\"OFF\","
                 "\"brightness\":40}]}", &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"07:00\",\"state\":\"ON\","
                 "\"brightness\":0}]}", &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"07:00\",\"state\":\"ON\","
                 "\"transition\":7201}]}", &s, err, sizeof err));
    CHECK(!parse("{\"entries\":[{\"days\":[1],\"time\":\"07:00\",\"state\":\"ON\","
                 "\"transition\":1.5}]}", &s, err, sizeof err));

    char big[2048] = "{\"entries\":[";
    for (int i = 0; i < SCHED_MAX + 1; i++) {
        strcat(big, i ? "," : "");
        strcat(big, "{\"days\":[1],\"time\":\"07:00\",\"state\":\"ON\"}");
    }
    strcat(big, "]}");
    CHECK(!parse(big, &s, err, sizeof err));
    CHECK(strstr(err, "at most 12") != NULL);
}

static void test_json_round_trip(void)
{
    sched_t s = morning_and_night();
    s.entry[2].enabled = false;
    cJSON *j = sched_to_json(&s);
    CHECK(j != NULL);
    char *text = cJSON_PrintUnformatted(j);
    CHECK_STR(text,
              "{\"entries\":["
              "{\"enabled\":true,\"days\":[1,2,3,4,5],\"time\":\"07:00\","
              "\"state\":\"ON\",\"brightness\":60,\"transition\":1800},"
              "{\"enabled\":true,\"days\":[1,2,3,4,5,6,7],\"time\":\"23:30\","
              "\"state\":\"OFF\",\"brightness\":null,\"transition\":0},"
              "{\"enabled\":false,\"days\":[6,7],\"time\":\"09:00\","
              "\"state\":\"ON\",\"brightness\":null,\"transition\":0}]}");

    sched_t back;
    char err[96];
    CHECK(sched_from_json(j, &back, err, sizeof err));
    CHECK(back.count == s.count);
    for (int i = 0; i < s.count; i++) {
        CHECK(back.entry[i].enabled == s.entry[i].enabled);
        CHECK(back.entry[i].days == s.entry[i].days);
        CHECK(back.entry[i].minute == s.entry[i].minute);
        CHECK(back.entry[i].on == s.entry[i].on);
        CHECK(back.entry[i].brightness == s.entry[i].brightness);
        CHECK(back.entry[i].transition_s == s.entry[i].transition_s);
    }
    cJSON_free(text);
    cJSON_Delete(j);
}

int main(void)
{
    test_validate();
    test_due();
    test_next();
    test_command();
    test_json_in();
    test_json_rejects();
    test_json_round_trip();
    REPORT();
}
