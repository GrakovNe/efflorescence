/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ota_logic.h"

#include <string.h>

#include "tinytest.h"

#define MANIFEST "https://grakovne.github.io/efflorescence/ota.json"

static void test_version_parse(void)
{
    int v[3];
    CHECK(ota_version_parse("v1.0.123", v) && v[0] == 1 && v[1] == 0 && v[2] == 123);
    CHECK(ota_version_parse("2.10.0", v) && v[0] == 2 && v[1] == 10 && v[2] == 0);

    CHECK(!ota_version_parse("", v));
    CHECK(!ota_version_parse("v", v));
    CHECK(!ota_version_parse("799349c", v));
    CHECK(!ota_version_parse("799349c-dirty", v));
    CHECK(!ota_version_parse("v1.0", v));
    CHECK(!ota_version_parse("v1.0.", v));
    CHECK(!ota_version_parse("v1.0.3.4", v));
    CHECK(!ota_version_parse("v1.0.3-dirty", v));
    CHECK(!ota_version_parse("v1..3", v));
    CHECK(!ota_version_parse("v1.0.12345678", v));
    CHECK(!ota_version_parse("V1.0.3", v));
}

static void test_version_newer(void)
{
    CHECK(ota_version_newer("v1.0.13", "v1.0.12"));
    CHECK(ota_version_newer("v1.0.100", "v1.0.99"));
    CHECK(ota_version_newer("v1.1.0", "v1.0.99"));
    CHECK(ota_version_newer("v2.0.0", "v1.9.9"));
    CHECK(!ota_version_newer("v1.0.12", "v1.0.12"));
    CHECK(!ota_version_newer("v1.0.11", "v1.0.12"));
    CHECK(!ota_version_newer("v0.9.99", "v1.0.0"));

    /* A development build takes any release; nothing takes a non-release. */
    CHECK(ota_version_newer("v1.0.1", "799349c"));
    CHECK(ota_version_newer("v1.0.1", "dev"));
    CHECK(!ota_version_newer("799349c", "v1.0.1"));
    CHECK(!ota_version_newer("dev", "abc1234"));
}

static bool parse(const char *json, const char *url, ota_manifest_t *m, char *err,
                  size_t err_sz)
{
    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL);
    err[0] = '\0';
    bool ok = ota_manifest_parse(root, url, m, err, err_sz);
    cJSON_Delete(root);
    return ok;
}

static void test_manifest(void)
{
    ota_manifest_t m;
    char err[96];

    CHECK(parse("{\"version\":\"v1.0.13\",\"app\":\"efflorescence-app.bin\"}",
                MANIFEST, &m, err, sizeof err));
    CHECK_STR(m.version, "v1.0.13");
    CHECK_STR(m.url, "https://grakovne.github.io/efflorescence/efflorescence-app.bin");

    CHECK(parse("{\"version\":\"v1.0.13\",\"app\":\"bin/app.bin\",\"name\":\"x\"}",
                "http://192.168.1.78:8000/ota.json", &m, err, sizeof err));
    CHECK_STR(m.url, "http://192.168.1.78:8000/bin/app.bin");

    CHECK(parse("{\"version\":\"v2.0.0\",\"app\":\"https://example.org/a.bin\"}",
                MANIFEST, &m, err, sizeof err));
    CHECK_STR(m.url, "https://example.org/a.bin");
}

static void test_manifest_rejects(void)
{
    ota_manifest_t m;
    char err[96];

    CHECK(!parse("[]", MANIFEST, &m, err, sizeof err));
    CHECK(!parse("{\"app\":\"a.bin\"}", MANIFEST, &m, err, sizeof err));
    CHECK(strstr(err, "version") != NULL);
    CHECK(!parse("{\"version\":\"abc1234\",\"app\":\"a.bin\"}", MANIFEST, &m, err,
                 sizeof err));
    CHECK(!parse("{\"version\":\"v1.0.1\"}", MANIFEST, &m, err, sizeof err));
    CHECK(strstr(err, "image") != NULL);
    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":7}", MANIFEST, &m, err, sizeof err));

    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":\"../a.bin\"}", MANIFEST, &m, err,
                 sizeof err));
    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":\"/a.bin\"}", MANIFEST, &m, err,
                 sizeof err));
    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":\"a b.bin\"}", MANIFEST, &m, err,
                 sizeof err));
    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":\"\"}", MANIFEST, &m, err,
                 sizeof err));
    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":\"ftp://x/a.bin\"}", MANIFEST, &m,
                 err, sizeof err));
    CHECK(!parse("{\"version\":\"v1.0.1\",\"app\":\"a.bin\"}", "ota.json", &m, err,
                 sizeof err));
    CHECK(m.url[0] == '\0' && m.version[0] == '\0');

    char json[400];
    char longname[260];
    memset(longname, 'a', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    snprintf(json, sizeof json, "{\"version\":\"v1.0.1\",\"app\":\"%s\"}", longname);
    CHECK(!parse(json, MANIFEST, &m, err, sizeof err));
    CHECK(strstr(err, "too long") != NULL);
    CHECK(m.url[0] == '\0');
}

static ota_health_t health(bool configured, bool sta, bool ap, bool web,
                           uint32_t uptime_s)
{
    ota_health_in_t in = {
        .wifi_configured = configured, .sta_connected = sta, .ap_up = ap,
        .web_up = web, .uptime_s = uptime_s,
    };
    return ota_health(&in);
}

static void test_health(void)
{
    /* On a home network: connected is the proof, however early. */
    CHECK(health(true, true, false, true, 3) == OTA_HEALTH_CONFIRM);
    CHECK(health(true, false, false, true, 3) == OTA_HEALTH_WAIT);
    CHECK(health(true, false, false, true, OTA_HEALTH_DEADLINE_S - 1) == OTA_HEALTH_WAIT);
    CHECK(health(true, false, false, true, OTA_HEALTH_DEADLINE_S) == OTA_HEALTH_ROLLBACK);
    /* The fallback AP does not count: the update came over the station. */
    CHECK(health(true, false, true, true, 150) == OTA_HEALTH_WAIT);
    CHECK(health(true, false, true, true, 200) == OTA_HEALTH_ROLLBACK);
    /* No web server, no next update. */
    CHECK(health(true, true, false, false, 60) == OTA_HEALTH_WAIT);
    CHECK(health(true, true, false, false, 200) == OTA_HEALTH_ROLLBACK);

    /* Not set up yet: the AP has to hold for a while. */
    CHECK(health(false, false, true, true, 5) == OTA_HEALTH_WAIT);
    CHECK(health(false, false, true, true, OTA_HEALTH_AP_SETTLE_S) == OTA_HEALTH_CONFIRM);
    CHECK(health(false, false, false, true, 60) == OTA_HEALTH_WAIT);
    CHECK(health(false, false, false, true, 200) == OTA_HEALTH_ROLLBACK);
}

int main(void)
{
    test_version_parse();
    test_version_newer();
    test_manifest();
    test_manifest_rejects();
    test_health();
    REPORT();
}
