/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "light_cmd.h"

#include <string.h>

#include "tinytest.h"

static bool parse(const char *json, light_cmd_t *cmd, char *err, size_t err_sz)
{
    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL);
    err[0] = '\0';
    bool ok = light_cmd_parse(root, cmd, err, err_sz);
    cJSON_Delete(root);
    return ok;
}

static void test_empty_keeps_everything(void)
{
    light_cmd_t c;
    char err[96];
    CHECK(parse("{}", &c, err, sizeof err));
    CHECK(c.state == LIGHT_KEEP && c.brightness == 0);
    CHECK(c.transition_ms == -1 && c.soft == -1);
}

static void test_state(void)
{
    light_cmd_t c;
    char err[96];
    CHECK(parse("{\"state\":\"ON\"}", &c, err, sizeof err) && c.state == LIGHT_ON);
    CHECK(parse("{\"state\":\"OFF\"}", &c, err, sizeof err) && c.state == LIGHT_OFF);
    CHECK(parse("{\"state\":\"TOGGLE\"}", &c, err, sizeof err) &&
          c.state == LIGHT_TOGGLE);

    CHECK(!parse("{\"state\":\"on\"}", &c, err, sizeof err));
    CHECK(strstr(err, "\"state\"") != NULL);
    CHECK(!parse("{\"state\":true}", &c, err, sizeof err));
    CHECK(!parse("[]", &c, err, sizeof err));
    CHECK(strstr(err, "JSON object") != NULL);
}

static void test_brightness(void)
{
    light_cmd_t c;
    char err[96];
    CHECK(parse("{\"brightness\":1}", &c, err, sizeof err) && c.brightness == 1);
    CHECK(parse("{\"brightness\":100}", &c, err, sizeof err) && c.brightness == 100);
    CHECK(c.state == LIGHT_KEEP);

    CHECK(!parse("{\"brightness\":0}", &c, err, sizeof err));
    CHECK(strstr(err, "between 1 and 100") != NULL);
    CHECK(!parse("{\"brightness\":101}", &c, err, sizeof err));
    CHECK(!parse("{\"brightness\":50.5}", &c, err, sizeof err));
    CHECK(!parse("{\"brightness\":\"50\"}", &c, err, sizeof err));
}

static void test_transition(void)
{
    light_cmd_t c;
    char err[96];
    CHECK(parse("{\"transition\":0}", &c, err, sizeof err) && c.transition_ms == 0);
    CHECK(parse("{\"transition\":0.2}", &c, err, sizeof err) &&
          c.transition_ms == 200);
    CHECK(parse("{\"transition\":1800}", &c, err, sizeof err) &&
          c.transition_ms == 1800000);
    CHECK(parse("{\"transition\":7200}", &c, err, sizeof err));

    CHECK(!parse("{\"transition\":-1}", &c, err, sizeof err));
    CHECK(!parse("{\"transition\":7201}", &c, err, sizeof err));
    CHECK(strstr(err, "7200") != NULL);
}

static void test_soft_and_all(void)
{
    light_cmd_t c;
    char err[96];
    CHECK(parse("{\"soft\":true}", &c, err, sizeof err) && c.soft == 1);
    CHECK(parse("{\"soft\":false}", &c, err, sizeof err) && c.soft == 0);
    CHECK(!parse("{\"soft\":1}", &c, err, sizeof err));

    /* What Home Assistant sends, plus fields it adds that we ignore. */
    CHECK(parse("{\"state\":\"ON\",\"brightness\":40,\"transition\":2,"
                "\"color_temp\":300}", &c, err, sizeof err));
    CHECK(c.state == LIGHT_ON && c.brightness == 40 && c.transition_ms == 2000);
}

static void test_text(void)
{
    light_cmd_t c;
    char err[96];
    CHECK(light_cmd_parse_text("ON", 2, &c, err, sizeof err) && c.state == LIGHT_ON);
    CHECK(c.brightness == 0 && c.transition_ms == -1 && c.soft == -1);
    CHECK(light_cmd_parse_text("OFF\n", 4, &c, err, sizeof err) && c.state == LIGHT_OFF);
    CHECK(light_cmd_parse_text("TOGGLE", 6, &c, err, sizeof err) &&
          c.state == LIGHT_TOGGLE);

    /* MQTT payloads are not terminated: only len bytes count. */
    const char *json = "{\"state\":\"ON\",\"brightness\":40}garbage";
    CHECK(light_cmd_parse_text(json, 30, &c, err, sizeof err));
    CHECK(c.state == LIGHT_ON && c.brightness == 40);
    CHECK(light_cmd_parse_text("ONLINE", 2, &c, err, sizeof err) && c.state == LIGHT_ON);

    CHECK(!light_cmd_parse_text("on", 2, &c, err, sizeof err));
    CHECK(strstr(err, "neither JSON") != NULL);
    CHECK(!light_cmd_parse_text("", 0, &c, err, sizeof err));
    CHECK(!light_cmd_parse_text("{\"brightness\":500}", 18, &c, err, sizeof err));
    CHECK(strstr(err, "between 1 and 100") != NULL);
}

static void test_state_json(void)
{
    cJSON *o = light_state_json(true, 60, false);
    CHECK(o != NULL);
    char *s = cJSON_PrintUnformatted(o);
    CHECK_STR(s, "{\"state\":\"ON\",\"brightness\":60,\"soft\":false}");
    cJSON_free(s);
    cJSON_Delete(o);

    o = light_state_json(false, 1, true);
    s = cJSON_PrintUnformatted(o);
    CHECK_STR(s, "{\"state\":\"OFF\",\"brightness\":1,\"soft\":true}");
    cJSON_free(s);
    cJSON_Delete(o);
}

int main(void)
{
    test_empty_keeps_everything();
    test_state();
    test_brightness();
    test_transition();
    test_soft_and_all();
    test_text();
    test_state_json();
    REPORT();
}
