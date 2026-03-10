#pragma once
#include <stdint.h>

void ha_discovery_publish_linkquality(uint16_t short_addr);
void ha_discovery_publish_temperature(uint16_t short_addr);
void ha_discovery_publish_humidity(uint16_t short_addr);
void ha_discovery_publish_battery(uint16_t short_addr);
void ha_discovery_publish_contact(uint16_t short_addr);
void ha_discovery_publish_motion(uint16_t short_addr);