/**
 * Based on:
 * SPDX-FileCopyrightText: 2023 Stephen Merrony
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "lwip/apps/mqtt.h"

#include "config.h"

#ifdef __cplusplus
extern "C"
{
#endif

bool mqtt_setup_client();
bool mqtt_connect(const char *id);
bool mqtt_ready();
bool mqtt_post(const char *topic, const char *msg);
bool mqtt_subscribeID (int id);

// this needs to be implemented outside:
void process_data (const char *topic, const u8_t *data, u16_t len, bool finalPart);

#ifdef __cplusplus
} // extern "C"
#endif
