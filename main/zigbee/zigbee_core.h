#pragma once

#include <stdbool.h>

void zigbee_core_init(void);
bool zigbee_core_start_pairing(void);
bool zigbee_core_is_pairing_active(void);
