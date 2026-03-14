#pragma once

#include <stdbool.h>

void mqtt_bridge_start(void);

void mqtt_publish_devices(const char *json);

void mqtt_publish_all_discovery(void);
void mqtt_publish_joined_device(const char *name, const char *ieee);
void mqtt_publish_device_presence(const char *ieee, bool occupied);
void mqtt_publish_pair_state(bool active);
void mqtt_publish_reset_state(bool active);
