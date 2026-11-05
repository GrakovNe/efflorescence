/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#include "appconf.h"

bool config_merge(const cJSON *root, appconf_t *c, char *err, size_t err_sz);
