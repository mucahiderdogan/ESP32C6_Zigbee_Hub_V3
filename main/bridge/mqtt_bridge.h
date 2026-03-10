#pragma once

void mqtt_bridge_start(void);

void mqtt_publish_devices(const char *json);

void mqtt_publish_ha_sensor(const char *name,
                            const char *id);