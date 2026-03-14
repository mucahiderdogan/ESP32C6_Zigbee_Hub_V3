#pragma once

#include <stdbool.h>
#include <stdint.h>

void zigbee_core_init(void);
bool zigbee_core_start_pairing(void);
bool zigbee_core_is_pairing_active(void);
bool zigbee_core_is_device_online(const char *ieee);
uint32_t zigbee_core_device_last_seen_seconds(const char *ieee);
