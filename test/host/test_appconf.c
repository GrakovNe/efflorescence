/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <string.h>

#include "tinytest.h"

static const char ID[] = "1a2b";

static void test_device_id(void)
{
    const uint8_t mac1[6] = { 0x44, 0xb1, 0x76, 0x5d, 0x1a, 0x2b };
    char id[5];
    appconf_device_id(mac1, id);
    CHECK_STR(id, "1a2b");

    const uint8_t mac2[6] = { 0, 0, 0, 0, 0xff, 0x0a };
    appconf_device_id(mac2, id);
    CHECK_STR(id, "ff0a");
}

static void test_hostname(void)
{
    char h[APPCONF_HOST_MAX + 1];

    appconf_hostname("Efflorescence-1A2B", ID, h, sizeof h);
    CHECK_STR(h, "efflorescence-1a2b");

    appconf_hostname("  --foo--bar--  ", ID, h, sizeof h);
    CHECK_STR(h, "foo-bar");

    appconf_hostname("a_b!", ID, h, sizeof h);
    CHECK_STR(h, "a-b");

    appconf_hostname("\xc3\x84\xc3\x96\xc3\x9c", ID, h, sizeof h);
    CHECK_STR(h, "efflorescence-1a2b");

    appconf_hostname("", ID, h, sizeof h);
    CHECK_STR(h, "efflorescence-1a2b");

    char tiny[4];
    appconf_hostname("Bedside lamp", ID, tiny, sizeof tiny);
    CHECK_STR(tiny, "bed");
}

static void test_defaults(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);

    CHECK_STR(c.device_name, "Efflorescence-1A2B");
    CHECK_STR(c.hostname, "efflorescence-1a2b");
    CHECK_STR(c.ntp_server, "pool.ntp.org");
    CHECK_STR(c.tz, "UTC0");
    CHECK(c.wifi_ssid[0] == '\0');
    CHECK(c.soft_switch == true);
    CHECK(c.battery == false);
    CHECK(c.mqtt_enabled == false);
    CHECK(c.mqtt_discovery == true);
    CHECK(c.mqtt_port == APPCONF_MQTT_PORT_PLAIN);
    CHECK_STR(c.mqtt_prefix, "efflorescence/1a2b");
    char topic[APPCONF_TOPIC_MAX + 1];
    appconf_topic(&c, "state", topic, sizeof topic);
    CHECK_STR(topic, "efflorescence/1a2b/state");
    /* The default name is also the setup AP's SSID. */
    CHECK(strlen(c.device_name) <= APPCONF_SSID_MAX);

    char err[128];
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_validate_name(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.device_name[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "Device name is empty.");

    appconf_defaults(&c, ID);
    strcpy(c.device_name, "lamp\x01");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "Device name has control characters.");

    appconf_defaults(&c, ID);
    strcpy(c.device_name, "\xd0\x9b\xd0\xb0\xd0\xbc\xd0\xbf\xd0\xb0");
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_validate_wifi(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Home");
    CHECK(appconf_validate(&c, err, sizeof err));

    strcpy(c.wifi_password, "1234567");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "Wi-Fi password must be at least 8 characters.");

    strcpy(c.wifi_password, "12345678");
    CHECK(appconf_validate(&c, err, sizeof err));

    strcpy(c.wifi_password, "pass\xd0\xb0word");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "Wi-Fi password must be printable ASCII.");

    appconf_defaults(&c, ID);
    strcpy(c.wifi_password, "12345678");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "Wi-Fi password without an SSID.");

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Ho\tme");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "Wi-Fi SSID has control characters.");
}

static void test_validate_ntp(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.ntp_server[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "NTP server is empty.");

    strcpy(c.ntp_server, "time server");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "NTP server has invalid characters.");

    strcpy(c.ntp_server, "192.168.1.1");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.ntp_server, "fd00::1");
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_validate_tz(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    strcpy(c.tz, "MSK-3");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.tz, "CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.tz, "<+0530>-5:30");
    CHECK(appconf_validate(&c, err, sizeof err));

    c.tz[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.tz, "Europe Moscow");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK(strstr(err, "POSIX TZ") != NULL);
}

static void test_validate_mqtt(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.mqtt_enabled = true;
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "MQTT host is empty.");

    strcpy(c.mqtt_host, "192.168.1.102");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_host, "broker host");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_host, "broker.local");

    c.mqtt_port = 0;
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "MQTT port must be 1-65535.");
    c.mqtt_port = 8883;

    strcpy(c.mqtt_password, "p\xd0\xb0ss");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_password, "secret");
    CHECK(appconf_validate(&c, err, sizeof err));

    /* A bad host does not matter while MQTT is off; the prefix always does. */
    appconf_defaults(&c, ID);
    strcpy(c.mqtt_host, "bad host");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "lamp/");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "lamp/+");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "my lamp");
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.mqtt_prefix[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "home/bedroom/lamp");
    CHECK(appconf_validate(&c, err, sizeof err));

    strcpy(c.mqtt_ca_cert, "not a certificate");
    CHECK(!appconf_validate(&c, err, sizeof err));
    CHECK_STR(err, "CA certificate does not look like PEM.");
    strcpy(c.mqtt_ca_cert, "-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n");
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_sanitize(void)
{
    static appconf_t c;
    appconf_defaults(&c, ID);
    CHECK(appconf_sanitize(&c, ID) == 0);

    /* One bad group falls back; the good ones survive. */
    strcpy(c.device_name, "Bedside");
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "short");
    strcpy(c.ntp_server, "ntp.example.org");
    strcpy(c.tz, "MSK-3");
    c.soft_switch = false;
    c.battery = true;
    c.mqtt_enabled = true;
    strcpy(c.mqtt_host, "192.168.1.102");
    strcpy(c.mqtt_prefix, "home/lamp");
    CHECK(appconf_sanitize(&c, ID) == 1);
    CHECK_STR(c.tz, "MSK-3");
    CHECK(c.battery == true);
    CHECK(c.mqtt_enabled && strcmp(c.mqtt_host, "192.168.1.102") == 0);
    CHECK_STR(c.mqtt_prefix, "home/lamp");

    /* A broken link turns MQTT off rather than leaving half of it. */
    strcpy(c.mqtt_host, "bad host");
    CHECK(appconf_sanitize(&c, ID) == 1);
    CHECK(!c.mqtt_enabled && c.mqtt_host[0] == '\0');
    CHECK_STR(c.mqtt_prefix, "home/lamp");
    CHECK_STR(c.device_name, "Bedside");
    CHECK_STR(c.device_name, "Bedside");
    CHECK_STR(c.hostname, "bedside");
    CHECK(c.wifi_ssid[0] == '\0' && c.wifi_password[0] == '\0');
    CHECK_STR(c.ntp_server, "ntp.example.org");
    CHECK(c.soft_switch == false);

    /* Strings of garbage; the flags are read through get_bool() in the
     * firmware and cannot hold anything but 0 or 1. */
    memset(&c, 0xff, sizeof c);
    c.soft_switch = true;
    c.battery = true;
    c.mqtt_enabled = true;
    c.mqtt_tls = true;
    c.mqtt_skip_verify = true;
    c.mqtt_discovery = true;
    CHECK(appconf_sanitize(&c, ID) >= 6);
    CHECK_STR(c.tz, "UTC0");
    CHECK_STR(c.mqtt_prefix, "efflorescence/1a2b");
    char err[128];
    CHECK(appconf_validate(&c, err, sizeof err));
    CHECK_STR(c.device_name, "Efflorescence-1A2B");
    CHECK_STR(c.hostname, "efflorescence-1a2b");
}

int main(void)
{
    test_device_id();
    test_hostname();
    test_defaults();
    test_validate_name();
    test_validate_wifi();
    test_validate_ntp();
    test_validate_tz();
    test_validate_mqtt();
    test_sanitize();
    REPORT();
}
