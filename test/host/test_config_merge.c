/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "config_merge.h"

#include <string.h>

#include "tinytest.h"

static const char ID[] = "1a2b";

static bool merge(appconf_t *c, const char *json, char *err, size_t err_sz)
{
    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL);
    err[0] = '\0';
    bool ok = config_merge(root, c, err, err_sz);
    cJSON_Delete(root);
    return ok;
}

static void test_omitted_fields_keep_values(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "secret123");
    c.soft_switch = false;

    CHECK(merge(&c, "{\"device\":{\"name\":\"Bedside\"}}", err, sizeof err));
    CHECK_STR(c.device_name, "Bedside");
    CHECK_STR(c.wifi_ssid, "Home");
    CHECK_STR(c.wifi_password, "secret123");
    CHECK(c.soft_switch == false);

    CHECK(merge(&c, "{}", err, sizeof err));
    CHECK_STR(c.device_name, "Bedside");
}

static void test_all_sections(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);

    CHECK(merge(&c, "{\"device\":{\"name\":\"Lamp\"},"
                    "\"wifi\":{\"ssid\":\"Home\",\"password\":\"secret123\"},"
                    "\"time\":{\"ntp_server\":\"ntp.example.org\",\"tz\":\"MSK-3\"},"
                    "\"light\":{\"soft\":false}}", err, sizeof err));
    CHECK_STR(c.device_name, "Lamp");
    CHECK_STR(c.wifi_ssid, "Home");
    CHECK_STR(c.wifi_password, "secret123");
    CHECK_STR(c.ntp_server, "ntp.example.org");
    CHECK_STR(c.tz, "MSK-3");
    CHECK(c.soft_switch == false);

    /* An open network: the page sends an empty password. */
    CHECK(merge(&c, "{\"wifi\":{\"ssid\":\"Guest\",\"password\":\"\"}}", err,
                sizeof err));
    CHECK_STR(c.wifi_ssid, "Guest");
    CHECK(c.wifi_password[0] == '\0');
}

static void test_mqtt(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);

    CHECK(!merge(&c, "{\"mqtt\":{\"enabled\":\"yes\"}}", err, sizeof err));
    CHECK(strstr(err, "true or false") != NULL);

    CHECK(!merge(&c, "{\"mqtt\":{\"enabled\":true}}", err, sizeof err));
    CHECK(strstr(err, "MQTT host is empty") != NULL);

    appconf_defaults(&c, ID);
    CHECK(merge(&c, "{\"mqtt\":{\"enabled\":true,\"host\":\"broker\",\"port\":8883,"
                    "\"tls\":true,\"topic_prefix\":\"home/lamp\",\"discovery\":false}}",
                err, sizeof err));
    CHECK(c.mqtt_enabled && c.mqtt_tls && c.mqtt_port == 8883 && !c.mqtt_discovery);
    CHECK_STR(c.mqtt_host, "broker");
    CHECK_STR(c.mqtt_prefix, "home/lamp");

    /* A password that is not sent stays; an empty one clears it. */
    strcpy(c.mqtt_password, "secret");
    CHECK(merge(&c, "{\"mqtt\":{\"username\":\"lamp\"}}", err, sizeof err));
    CHECK_STR(c.mqtt_password, "secret");
    CHECK(merge(&c, "{\"mqtt\":{\"password\":\"\"}}", err, sizeof err));
    CHECK(c.mqtt_password[0] == '\0');

    CHECK(!merge(&c, "{\"mqtt\":{\"port\":70000}}", err, sizeof err));
    CHECK(strstr(err, "between 1 and 65535") != NULL);
    CHECK(!merge(&c, "{\"mqtt\":{\"port\":1883.5}}", err, sizeof err));
    CHECK(!merge(&c, "{\"mqtt\":{\"topic_prefix\":\"lamp/\"}}", err, sizeof err));
}

static void test_rejects(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);

    CHECK(!merge(&c, "{\"wifi\":\"Home\"}", err, sizeof err));
    CHECK_STR(err, "\"wifi\" must be an object");

    CHECK(!merge(&c, "{\"light\":{\"soft\":\"yes\"}}", err, sizeof err));
    CHECK(strstr(err, "true or false") != NULL);

    CHECK(!merge(&c, "{\"device\":{\"name\":null}}", err, sizeof err));
    CHECK(strstr(err, "must be a string") != NULL);

    CHECK(!merge(&c, "{\"device\":{\"name\":\"\"}}", err, sizeof err));
    CHECK_STR(err, "Device name is empty.");

    appconf_defaults(&c, ID);
    CHECK(merge(&c, "{\"device\":{\"name\":\"abcdefghijklmnopqrstuvwxyz012345\"}}",
                err, sizeof err));
    CHECK(strlen(c.device_name) == 32);
    CHECK(!merge(&c, "{\"device\":{\"name\":\"abcdefghijklmnopqrstuvwxyz0123456\"}}",
                 err, sizeof err));
    CHECK(strstr(err, "at most 32 bytes") != NULL);

    appconf_defaults(&c, ID);
    CHECK(!merge(&c, "{\"time\":{\"tz\":\"Europe Moscow\"}}", err, sizeof err));
    CHECK(strstr(err, "POSIX TZ") != NULL);

    appconf_defaults(&c, ID);
    CHECK(!merge(&c, "{\"wifi\":{\"ssid\":\"Home\",\"password\":\"short\"}}", err,
                 sizeof err));
    CHECK(strstr(err, "at least 8") != NULL);
}

int main(void)
{
    test_omitted_fields_keep_values();
    test_all_sections();
    test_mqtt();
    test_rejects();
    REPORT();
}
