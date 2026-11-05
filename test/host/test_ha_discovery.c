/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ha_discovery.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "tinytest.h"

static const ha_dev_t DEV = {
    .prefix = "efflorescence/c6e0",
    .device_id = "c6e0",
    .name = "Bedside \"lamp\"",
    .hostname = "bedside-lamp",
    .version = "v1.0.12",
};

static const char *str(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static cJSON *config(ha_entity_t e)
{
    char *s = ha_config_json(&DEV, e);
    CHECK(s != NULL);
    cJSON *o = cJSON_Parse(s);
    CHECK(o != NULL);
    free(s);
    return o;
}

static void test_topics(void)
{
    char t[HA_TOPIC_MAX];
    ha_config_topic(&DEV, HA_LIGHT, t, sizeof t);
    CHECK_STR(t, "homeassistant/light/efflorescence_c6e0_light/config");
    ha_config_topic(&DEV, HA_SOFT_SWITCH, t, sizeof t);
    CHECK_STR(t, "homeassistant/switch/efflorescence_c6e0_soft/config");
    ha_config_topic(&DEV, HA_TAPS, t, sizeof t);
    CHECK_STR(t, "homeassistant/event/efflorescence_c6e0_taps/config");
    ha_config_topic(&DEV, HA_UPDATE, t, sizeof t);
    CHECK_STR(t, "homeassistant/update/efflorescence_c6e0_firmware/config");
}

static void test_light(void)
{
    cJSON *o = config(HA_LIGHT);
    CHECK_STR(str(o, "unique_id"), "efflorescence_c6e0_light");
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(o, "name")));
    CHECK_STR(str(o, "schema"), "json");
    CHECK_STR(str(o, "command_topic"), "efflorescence/c6e0/set");
    CHECK_STR(str(o, "state_topic"), "efflorescence/c6e0/state");
    CHECK_STR(str(o, "availability_topic"), "efflorescence/c6e0/availability");
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "brightness")));
    CHECK(cJSON_GetObjectItemCaseSensitive(o, "brightness_scale")->valuedouble == 100);
    const cJSON *modes = cJSON_GetObjectItemCaseSensitive(o, "supported_color_modes");
    CHECK(cJSON_GetArraySize(modes) == 1);
    CHECK_STR(cJSON_GetArrayItem(modes, 0)->valuestring, "brightness");

    const cJSON *dev = cJSON_GetObjectItemCaseSensitive(o, "device");
    CHECK_STR(str(dev, "identifiers"), "efflorescence_c6e0");
    CHECK_STR(str(dev, "name"), "Bedside \"lamp\"");
    CHECK_STR(str(dev, "sw_version"), "v1.0.12");
    CHECK_STR(str(dev, "configuration_url"), "http://bedside-lamp.local/");
    cJSON_Delete(o);
}

static void test_soft_switch(void)
{
    cJSON *o = config(HA_SOFT_SWITCH);
    CHECK_STR(str(o, "command_topic"), "efflorescence/c6e0/set");
    CHECK_STR(str(o, "payload_on"), "{\"soft\":true}");
    CHECK_STR(str(o, "payload_off"), "{\"soft\":false}");
    CHECK_STR(str(o, "state_topic"), "efflorescence/c6e0/state");
    CHECK(strstr(str(o, "value_template"), "value_json.soft") != NULL);
    CHECK_STR(str(o, "entity_category"), "config");
    CHECK_STR(str(cJSON_GetObjectItemCaseSensitive(o, "device"), "identifiers"),
              "efflorescence_c6e0");
    cJSON_Delete(o);
}

static void test_taps(void)
{
    cJSON *o = config(HA_TAPS);
    CHECK_STR(str(o, "state_topic"), "efflorescence/c6e0/event");
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o, "event_types")) == 4);
    cJSON_Delete(o);

    CHECK(ha_taps_json(0) == NULL && ha_taps_json(1) == NULL);
    char *s = ha_taps_json(2);
    CHECK_STR(s, "{\"event_type\":\"double\",\"count\":2}");
    free(s);
    s = ha_taps_json(3);
    CHECK_STR(s, "{\"event_type\":\"triple\",\"count\":3}");
    free(s);
    s = ha_taps_json(4);
    CHECK_STR(s, "{\"event_type\":\"quadruple\",\"count\":4}");
    free(s);
    s = ha_taps_json(9);
    CHECK_STR(s, "{\"event_type\":\"many\",\"count\":9}");
    free(s);
}

static void test_update(void)
{
    cJSON *o = config(HA_UPDATE);
    CHECK_STR(str(o, "state_topic"), "efflorescence/c6e0/update");
    CHECK_STR(str(o, "command_topic"), "efflorescence/c6e0/update/install");
    CHECK_STR(str(o, "payload_install"), "install");
    CHECK_STR(str(o, "device_class"), "firmware");
    cJSON_Delete(o);

    char *s = ha_update_json("v1.0.12", "v1.0.13", false, -1);
    CHECK_STR(s, "{\"installed_version\":\"v1.0.12\",\"latest_version\":\"v1.0.13\","
                 "\"in_progress\":false,\"update_percentage\":null}");
    free(s);
    /* Nothing checked yet: nothing to offer. */
    s = ha_update_json("54fda4b", "", false, -1);
    CHECK(strstr(s, "\"latest_version\":\"54fda4b\"") != NULL);
    free(s);
    s = ha_update_json("54fda4b", NULL, true, 43);
    CHECK(strstr(s, "\"latest_version\":\"54fda4b\"") != NULL);
    CHECK(strstr(s, "\"in_progress\":true,\"update_percentage\":43") != NULL);
    free(s);
}

int main(void)
{
    test_topics();
    test_light();
    test_soft_switch();
    test_taps();
    test_update();
    REPORT();
}
