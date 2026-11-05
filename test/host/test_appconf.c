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
    CHECK(c.wifi_ssid[0] == '\0');
    CHECK(c.soft_switch == true);
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

static void test_sanitize(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);
    CHECK(appconf_sanitize(&c, ID) == 0);

    /* One bad group falls back; the good ones survive. */
    strcpy(c.device_name, "Bedside");
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "short");
    strcpy(c.ntp_server, "ntp.example.org");
    c.soft_switch = false;
    CHECK(appconf_sanitize(&c, ID) == 1);
    CHECK_STR(c.device_name, "Bedside");
    CHECK_STR(c.hostname, "bedside");
    CHECK(c.wifi_ssid[0] == '\0' && c.wifi_password[0] == '\0');
    CHECK_STR(c.ntp_server, "ntp.example.org");
    CHECK(c.soft_switch == false);

    memset(&c, 0xff, sizeof c);
    c.soft_switch = true;
    CHECK(appconf_sanitize(&c, ID) == 3);
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
    test_sanitize();
    REPORT();
}
